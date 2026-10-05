#include "app_core.h"
#include "json.hpp"
#include "unicode_controls.h"
#include "tool_check.h"
#include <cstdlib>
#include <iostream>
#include <set>
#include <stdexcept>
#include <sys/wait.h>
#include <unistd.h>

bool printable(const std::string & s, bool allow_tab) {
    // UTF-8 validity is enforced by the JSON decoder. Reject Unicode C* and separators.
    for (size_t i=0; i<s.size();) {
        unsigned char b=s[i++];
        uint32_t c=b;
        unsigned continuation=0;
        if (b>=0xf0) {c=b&7; continuation=3;}
        else if (b>=0xe0) {c=b&15; continuation=2;}
        else if (b>=0xc0) {c=b&31; continuation=1;}
        else if (b>=0x80) return false;
        if (i+continuation>s.size()) return false;
        while (continuation--) {
            b=s[i++];
            if ((b&0xc0)!=0x80) return false;
            c=(c<<6)|(b&63);
        }
        if (allow_tab && c=='\t') continue;
        for (const auto & range : forbidden_unicode)
            if (c>=range[0] && c<=range[1]) return false;
    }
    return true;
}

Result parse_result(const std::string & text) {
    std::set<std::string> keys;
    auto callback = [&keys](int, nlohmann::json::parse_event_t event, nlohmann::json & parsed) {
        if (event==nlohmann::json::parse_event_t::key && !keys.insert(parsed.get<std::string>()).second)
            throw std::runtime_error("duplicate result key");
        return true;
    };
    auto j=nlohmann::json::parse(text,callback);
    if (!j.is_object() || j.size()!=2 || !j.contains("kind") || !j.contains("value") ||
        !j["kind"].is_string() || !j["value"].is_string())
        throw std::runtime_error("expected kind and value strings");
    Result r{j["kind"],j["value"]};
    if (r.kind!="COMMAND" && r.kind!="CLARIFY" && r.kind!="UNABLE")
        throw std::runtime_error("invalid outcome");
    if (r.value.empty() || r.value.size()>4096 ||
        r.value.find_first_not_of(" \t")==std::string::npos ||
        !printable(r.value,r.kind=="COMMAND"))
        throw std::runtime_error("empty, oversized or invisible result");
    return r;
}

static bool syntax_ok(const std::string & command) {
    pid_t child=fork();
    if (child<0) throw std::runtime_error("cannot start syntax checker");
    if (child==0) {
        clearenv();
        setenv("PATH","/usr/bin:/bin",1);
        setenv("LC_ALL","C",1);
        execl("/bin/bash","bash","--noprofile","--norc","-n","-c",command.c_str(),nullptr);
        _exit(127);
    }
    int status=0;
    while (waitpid(child,&status,0)<0) {
        if (errno!=EINTR) throw std::runtime_error("syntax checker wait failed");
    }
    return WIFEXITED(status) && WEXITSTATUS(status)==0;
}

int handle_result(const Result & r, bool preview) {
    if (r.kind=="COMMAND" && !syntax_ok(r.value))
        throw std::runtime_error("model returned invalid Bash syntax; nothing executed");
    if (r.kind=="COMMAND") check_tools(r.value);
    if (preview) {
        std::cout << nlohmann::json{{"kind",r.kind},{"value",r.value}}.dump() << '\n';
        return 0;
    }
    if (r.kind!="COMMAND") {
        std::cout << r.kind << ": " << r.value << '\n';
        return r.kind=="CLARIFY" ? 2 : 3;
    }
    std::cout << "Command:\n" << r.value << "\n" << std::flush;
    if (r.value.find('\t')!=std::string::npos)
        std::cout << "Command with TAB escaped: " << nlohmann::json(r.value).dump() << "\n" << std::flush;
    if (!isatty(STDIN_FILENO) || !isatty(STDOUT_FILENO)) {
        std::cerr << "Not executed: interactive input and output terminals are required.\n";
        return 4;
    }
    std::cout << "Execute? [Y/n] " << std::flush;
    std::string answer;
    if (!std::getline(std::cin,answer) || (!answer.empty() && answer!="y" && answer!="Y")) {
        std::cout << "Not executed.\n";
        return 4;
    }
    // Do not allow startup files to introduce commands absent from the preview.
    unsetenv("BASH_ENV");
    unsetenv("ENV");
    execl("/bin/bash","bash","--noprofile","--norc","-c",r.value.c_str(),nullptr);
    throw std::runtime_error("cannot execute /bin/bash");
}
