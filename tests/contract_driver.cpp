// Test-only entry point into production parsing/confirmation code. Never installed.
#include "app_core.h"
#include <iostream>
int main(int argc,char ** argv) {
    try {
        if (argc<2) return 1;
        return handle_result(parse_result(argv[1]),argc==3);
    } catch (const std::exception & e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
