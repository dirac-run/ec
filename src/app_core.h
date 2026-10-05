#pragma once
#include <string>
struct Result { std::string kind; std::string value; };
Result parse_result(const std::string & text);
bool printable(const std::string & text, bool allow_tab = false);
int handle_result(const Result & result, bool preview);
