#pragma once
#include "llama.h"
#include <stdexcept>
#include <string>
#include <vector>
inline std::string model_architecture(const llama_model *m) {
    char b[128];
    int n = llama_model_meta_val_str(m, "general.architecture", b, sizeof(b));
    if (n < 0 || n >= 128) throw std::runtime_error("invalid architecture");
    return std::string(b, n);
}
inline void require_command_architecture(const std::string &architecture) {
    if (architecture != "qwen2" && architecture != "qwen3")
        throw std::runtime_error("this command-only runtime requires a qualified qwen2 or qwen3 model");
}
inline const char *model_system_prompt(const llama_model *m, const char *system) {
    require_command_architecture(model_architecture(m));
    return system;
}
inline std::string architecture_prompt_text(const std::string &architecture,
                                          const char *system,
                                          const std::string &request) {
    require_command_architecture(architecture);
    std::string prompt = std::string("<|im_start|>system\n") + system +
        "<|im_end|>\n<|im_start|>user\n" + request +
        "<|im_end|>\n<|im_start|>assistant\n";
    // Exact saved Qwen3 chat_template, add_generation_prompt=true,
    // enable_thinking=false. Qwen2 formatting remains byte-identical.
    if (architecture == "qwen3") prompt += "<think>\n\n</think>\n\n";
    return prompt;
}
inline std::string request_prompt_text(const llama_model *m, const char *system,
                                      const std::string &request) {
    return architecture_prompt_text(model_architecture(m), system, request);
}
inline std::vector<llama_token> tokenize_prompt(const llama_vocab *v,
                                               const std::string &s) {
    int n = -llama_tokenize(v, s.data(), s.size(), nullptr, 0, false, true);
    if (n <= 0) throw std::runtime_error("tokenization failed");
    std::vector<llama_token> tokens(n);
    if (llama_tokenize(v, s.data(), s.size(), tokens.data(), n, false, true) != n)
        throw std::runtime_error("tokenization failed");
    return tokens;
}
inline std::vector<llama_token> request_tokens(const llama_model *m,
                                             const char *system,
                                             const std::string &request) {
    return tokenize_prompt(llama_model_get_vocab(m), request_prompt_text(m, system, request));
}
