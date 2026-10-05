#include "prompt_format.h"
#include "prompt.h"
#include "json.hpp"
#include <iostream>
int main(int argc,char**argv){ if(argc!=2)return 1; llama_log_set([](ggml_log_level,const char*,void*){},nullptr); llama_backend_init(); auto p=llama_model_default_params();p.vocab_only=true;p.n_gpu_layers=0;auto*m=llama_model_load_from_file(argv[1],p);if(!m)return 1;std::string line;while(std::getline(std::cin,line)){auto r=nlohmann::json::parse(line).get<std::string>();std::cout<<nlohmann::json(request_tokens(m,system_prompt,r)).dump()<<'\n';}llama_model_free(m);}
