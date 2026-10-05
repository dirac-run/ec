#include "app_core.h"
#include "local_socket.h"
#include "prompt.h"
#include "prompt_format.h"
#include "runtime_config.h"
#include "llama.h"
#include "json.hpp"
#include <algorithm>
#include <array>
#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <memory>
#include <poll.h>
#include <stdexcept>
#include <string>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

static std::string default_model() {
    const char * data=std::getenv("XDG_DATA_HOME");
    if (data && *data) return std::string(data)+"/easycommand/model.gguf";
    const char * home=std::getenv("HOME");
    if (!home || !*home) throw std::runtime_error("set HOME or XDG_DATA_HOME, or pass --model");
    return std::string(home)+"/.local/share/easycommand/model.gguf";
}

using model_ptr=std::unique_ptr<llama_model,decltype(&llama_model_free)>;
using context_ptr=std::unique_ptr<llama_context,decltype(&llama_free)>;

static model_ptr load_model(const std::string & path) {
    auto mp=llama_model_default_params();
    mp.n_gpu_layers=0;
    model_ptr model(
        llama_model_load_from_file(path.c_str(),mp),llama_model_free);
    if (!model) throw std::runtime_error("could not load local model: "+path);
    return model;
}

static context_ptr make_context(llama_model * model,int threads) {
    auto cp=llama_context_default_params();
    cp.n_ctx=runtime_context;
    cp.n_batch=runtime_context;
    cp.n_threads=threads;
    cp.n_threads_batch=threads;
    context_ptr ctx(llama_init_from_model(model,cp),llama_free);
    if (!ctx) throw std::runtime_error("inference context allocation failed");
    return ctx;
}

static std::string generate_with_context(llama_model * model,llama_context * ctx,
                                         const std::string & request,const std::string & forced_kind) {
    const auto * vocab=llama_model_get_vocab(model);
    auto tokens=request_tokens(model,model_system_prompt(model,system_prompt),request);
    int nt=static_cast<int>(tokens.size());
    constexpr int context=runtime_context, output_limit=runtime_output_limit;
    if (nt<=0 || nt>context-output_limit) throw std::runtime_error("request exceeds context limit; shorten it");
    std::unique_ptr<llama_sampler,decltype(&llama_sampler_free)> sampler(
        llama_sampler_chain_init(llama_sampler_chain_default_params()),llama_sampler_free);
    llama_sampler_chain_add(sampler.get(),llama_sampler_init_greedy());
    // A resident Qwen3.5 context has both attention KV and recurrent memory.
    // Clear both before every request; each call also owns a fresh sampler.
    llama_synchronize(ctx);
    llama_memory_clear(llama_get_memory(ctx),true);
    auto batch=llama_batch_get_one(tokens.data(),tokens.size());
    std::string response;
    llama_token token=0;
    for (int i=0;i<output_limit;++i) {
        if (llama_decode(ctx,batch)) throw std::runtime_error("inference failed");
        token=llama_sampler_sample(sampler.get(),ctx,-1);
        if (token==151645) return response;
        char piece[256];
        int n=llama_token_to_piece(vocab,token,piece,sizeof(piece),0,true);
        if (n<0) throw std::runtime_error("oversized token piece");
        response.append(piece,n);
        batch=llama_batch_get_one(&token,1);
    }
    throw std::runtime_error("model response exceeded output limit; nothing executed");
}

static std::string generate(llama_model * model,const std::string & request,
                            int threads,const std::string & forced_kind) {
    auto ctx=make_context(model,threads);
    return generate_with_context(model,ctx.get(),request,forced_kind);
}

static std::string infer(const std::string & path,const std::string & router_path,
                         const std::string & request,int threads) {
    llama_log_set([](ggml_log_level level,const char * text,void *) {
        if (level==GGML_LOG_LEVEL_ERROR) std::cerr << text;
    },nullptr);
    llama_backend_init();
    auto model=load_model(path);
    return generate(model.get(),request,threads,"");
}

static std::string model_identity(const std::string &path) {
    auto canonical=std::filesystem::canonical(path).string(); struct stat st{};
    if(::stat(canonical.c_str(),&st)!=0 || !S_ISREG(st.st_mode)) throw std::runtime_error("cannot identify model");
    return canonical+":"+std::to_string(st.st_dev)+":"+std::to_string(st.st_ino)+":"+std::to_string(st.st_size)+":"+std::to_string(st.st_mtim.tv_sec)+":"+std::to_string(st.st_mtim.tv_nsec);
}
static Result command_result(const std::string &raw) {
    if(raw.find_first_of("\n\r")!=std::string::npos) throw std::runtime_error("expected one compact JSON line");
    auto result=parse_result(raw);
    if(result.kind!="COMMAND") throw std::runtime_error("expected COMMAND result; nothing executed");
    return result;
}
static volatile std::sig_atomic_t stop_server=0;
static void stop_signal(int) { stop_server=1; }

static int serve(const std::string & path,const std::string & model_path,int threads) {
    llama_log_set([](ggml_log_level level,const char * text,void *) {
        if (level==GGML_LOG_LEVEL_ERROR) std::cerr << text;
    },nullptr);
    llama_backend_init();
    const auto loaded_identity=model_identity(model_path);
    auto model=load_model(model_path);
    model_system_prompt(model.get(),system_prompt);
    if(model_identity(model_path)!=loaded_identity) throw std::runtime_error("model changed while loading");
    auto ctx=make_context(model.get(),threads);
    auto server=local_socket::listen_private(path);
    struct remove_socket {
        const std::string & path;
        ~remove_socket() { ::unlink(path.c_str()); }
    } cleanup{path};
    std::signal(SIGTERM,stop_signal);
    std::signal(SIGINT,stop_signal);
    while (!stop_server) {
        pollfd entry{server.get(),POLLIN,0};
        int polled=::poll(&entry,1,1000);
        if (polled<0) {
            if (errno==EINTR) continue;
            throw std::runtime_error("resident accept poll failed");
        }
        if (!polled) continue;
        local_socket::fd client(::accept4(server.get(),nullptr,nullptr,SOCK_CLOEXEC));
        if (client.get()<0) {
            if (errno==EINTR) continue;
            throw std::runtime_error("resident accept failed");
        }
        try {
            auto envelope=nlohmann::json::parse(local_socket::receive_frame(client.get(),8192,5));
            if (!envelope.is_object() || envelope.size()!=4 || envelope.value("version",0)!=2 ||
                !envelope.contains("request") || !envelope["request"].is_string() ||
                !envelope.contains("expected_model") || !envelope["expected_model"].is_string() ||
                !envelope.contains("check_only") || !envelope["check_only"].is_boolean())
                throw std::runtime_error("invalid resident request envelope");
            if(envelope["expected_model"]!=loaded_identity || model_identity(model_path)!=loaded_identity)
                throw std::runtime_error("resident model differs from requested pin; restart required");
            auto request=envelope["request"].get<std::string>();
            const bool check_only=envelope["check_only"].get<bool>();
            if (!check_only && (request.empty() || request.find('\0')!=std::string::npos))
                throw std::runtime_error("invalid resident request text");
            auto raw=check_only ? "" : generate_with_context(model.get(),ctx.get(),request,"");
            local_socket::send_frame(client.get(),nlohmann::json{{"version",2},{"ok",true},
                {"raw",raw},{"model_identity",loaded_identity}}.dump(),131072,5);
        } catch (const std::exception & error) {
            // A malformed or disconnected client must not terminate the worker.
            try {
                local_socket::send_frame(client.get(),nlohmann::json{{"version",2},{"ok",false},
                    {"error",error.what()}}.dump(),131072,1);
            } catch (const std::exception &) {}
        }
    }
    return 0;
}

static std::string infer_resident(const std::string & path,const std::string & request,const std::string &expected_path,bool check_only) {
    const auto expected=model_identity(expected_path);
    auto client=local_socket::connect_retry(path,5000);
    local_socket::send_frame(client.get(),nlohmann::json{{"version",2},{"expected_model",expected},{"check_only",check_only},
        {"request",request}}.dump(),8192,5);
    auto envelope=nlohmann::json::parse(local_socket::receive_frame(client.get(),131072,300));
    if (!envelope.is_object() || (envelope.size()!=3 && envelope.size()!=4) || !envelope.contains("version") ||
        !envelope.contains("ok") || envelope["version"]!=2 || !envelope["ok"].is_boolean())
        throw std::runtime_error("invalid resident response envelope");
    if (envelope["ok"]==false) {
        if (!envelope.contains("error") || !envelope["error"].is_string())
            throw std::runtime_error("invalid resident error response");
        throw std::runtime_error("resident worker: "+envelope["error"].get<std::string>());
    }
    if (!envelope.contains("raw") || !envelope["raw"].is_string())
        throw std::runtime_error("invalid resident success response");
    if(envelope.value("model_identity",std::string())!=expected || model_identity(expected_path)!=expected)
        throw std::runtime_error("resident returned different model identity");
    return envelope["raw"].get<std::string>();
}

int main(int argc,char ** argv) {
    try {
        std::string path,router_path,request,socket_path,expected_path;
        bool check_only=false;
        bool preview=false,server=false,explicit_threads=false,explicit_socket=false,request_started=false;
        int threads=runtime_threads;
        for (int i=1;i<argc;++i) {
            std::string arg=argv[i];
            if (request_started) {
                if (!request.empty()) request+=' ';
                request+=arg;
                continue;
            }
            if (arg=="--") { request_started=true; continue; }
            if (arg=="--help") {
                std::cout << "Usage: ec [--model PATH] [--threads N] [--preview] 'request'\n"
                          << "       ec --serve --socket PATH --model PATH --threads N\n"
                          << "       ec --socket PATH --expect-model PATH [--preview] 'request'\n"
                          << "--preview prints the validated JSON result without executing.\n"
                          << "Execution requires interactive confirmation (Enter, y or Y).\n";
                return 0;
            } else if (arg=="--model" && i+1<argc) path=argv[++i];
            else if (arg=="--router-model" && i+1<argc) router_path=argv[++i];
            else if (arg=="--socket" && i+1<argc) { socket_path=argv[++i]; explicit_socket=true; }
            else if (arg=="--serve") server=true;
            else if(arg=="--expect-model" && i+1<argc) expected_path=argv[++i];
            else if(arg=="--resident-model-check") check_only=true;
            else if (arg=="--threads" && i+1<argc) {
                std::string n=argv[++i];
                size_t used=0;
                threads=std::stoi(n,&used);
                if (used!=n.size() || threads<1 || threads>16) throw std::runtime_error("threads must be 1..16");
                explicit_threads=true;
            } else if (arg=="--preview") preview=true;
            else if (arg.rfind("--",0)==0) throw std::runtime_error("unknown or incomplete option: "+arg);
            else { request=arg; request_started=true; }
        }
        if (explicit_socket && socket_path.empty())
            throw std::runtime_error("--socket path must be nonempty");
        if(!router_path.empty()) throw std::runtime_error("router models are disabled in COMMAND-only runtime");
        if (server) {
            if (socket_path.empty() || path.empty() || !request.empty() || preview ||
                !router_path.empty() || !expected_path.empty() || check_only)
                throw std::runtime_error("--serve requires --socket and --model only; no request/router/preview");
            if (!std::filesystem::is_regular_file(path))
                throw std::runtime_error("model file missing: "+path);
            return serve(socket_path,path,threads);
        }
        if (request.empty() && !check_only) throw std::runtime_error("provide an English request; see --help");
        if (!socket_path.empty()) {
            if (!path.empty() || !router_path.empty() || explicit_threads || expected_path.empty())
                throw std::runtime_error("resident client accepts --socket, --preview, and request only");
            auto raw=infer_resident(socket_path,request,expected_path,check_only);
            if(check_only) { std::cout << "Resident matches requested model.\n"; return 0; }
            return handle_result(command_result(raw),preview);
        }
        if(check_only || !expected_path.empty()) throw std::runtime_error("model checks require resident socket");
        if (path.empty()) path=default_model();
        if (!std::filesystem::is_regular_file(path)) throw std::runtime_error("model file missing: "+path);
        if (!router_path.empty() && !std::filesystem::is_regular_file(router_path))
            throw std::runtime_error("router model file missing: "+router_path);
        auto result=command_result(infer(path,router_path,request,threads));
        return handle_result(result,preview);
    } catch (const std::exception & e) {
        std::cerr << "ec: application error: " << e.what() << '\n';
        return 1;
    }
}
