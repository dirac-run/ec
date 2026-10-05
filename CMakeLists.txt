cmake_minimum_required(VERSION 3.20)
project(easycommand VERSION 0.1.0 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
include(FetchContent)
include(CTest)

option(EC_NATIVE "Optimize for the build machine's CPU" OFF)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(GGML_CUDA OFF CACHE BOOL "" FORCE)
set(GGML_NATIVE ${EC_NATIVE} CACHE BOOL "" FORCE)
set(LLAMA_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(LLAMA_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(LLAMA_BUILD_SERVER OFF CACHE BOOL "" FORCE)
set(LLAMA_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
FetchContent_Declare(llama_cpp
    URL https://codeload.github.com/ggml-org/llama.cpp/tar.gz/1af554f8fc78ba029665a47b839484d9763e2a75
    URL_HASH SHA256=4d12a5e5d9fe011e0773f9b532a8259771c99a2998d4549a59833f7a6da932bf)
FetchContent_Declare(tree_sitter
    URL https://codeload.github.com/tree-sitter/tree-sitter/tar.gz/refs/tags/v0.25.10
    URL_HASH SHA256=ad5040537537012b16ef6e1210a572b927c7cdc2b99d1ee88d44a7dcdc3ff44c
    SOURCE_SUBDIR _source_only)
FetchContent_Declare(tree_sitter_bash
    URL https://codeload.github.com/tree-sitter/tree-sitter-bash/tar.gz/refs/tags/v0.25.1
    URL_HASH SHA256=2e785a761225b6c433410ef9c7b63cfb0a4e83a35a19e0f2aec140b42c06b52d
    SOURCE_SUBDIR _source_only)
FetchContent_MakeAvailable(llama_cpp tree_sitter tree_sitter_bash)

file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/include")
set(JSON_HEADER "${CMAKE_BINARY_DIR}/include/json.hpp")
if(NOT EXISTS "${JSON_HEADER}")
    file(DOWNLOAD https://raw.githubusercontent.com/nlohmann/json/v3.12.0/single_include/nlohmann/json.hpp
        "${JSON_HEADER}" TLS_VERIFY ON
        EXPECTED_HASH SHA256=aaf127c04cb31c406e5b04a63f1ae89369fccde6d8fa7cdda1ed4f32dfc5de63)
else()
    file(SHA256 "${JSON_HEADER}" JSON_SHA)
    if(NOT JSON_SHA STREQUAL "aaf127c04cb31c406e5b04a63f1ae89369fccde6d8fa7cdda1ed4f32dfc5de63")
        message(FATAL_ERROR "JSON dependency checksum differs from the pinned source")
    endif()
endif()
file(READ config/system-prompt.txt SYSTEM_PROMPT)
configure_file(src/prompt.h.in generated/prompt.h @ONLY)
file(READ config/runtime.json RUNTIME_CONFIG)
string(JSON RUNTIME_CONTEXT GET "${RUNTIME_CONFIG}" context)
string(JSON RUNTIME_OUTPUT_LIMIT GET "${RUNTIME_CONFIG}" output_limit)
string(JSON RUNTIME_THREADS GET "${RUNTIME_CONFIG}" threads)
configure_file(src/runtime_config.h.in generated/runtime_config.h @ONLY)

add_library(shell_parser STATIC "${tree_sitter_SOURCE_DIR}/lib/src/lib.c"
    "${tree_sitter_bash_SOURCE_DIR}/src/parser.c" "${tree_sitter_bash_SOURCE_DIR}/src/scanner.c")
target_include_directories(shell_parser PUBLIC "${tree_sitter_SOURCE_DIR}/lib/include"
    PRIVATE "${tree_sitter_SOURCE_DIR}/lib/src" "${tree_sitter_bash_SOURCE_DIR}/src")
add_library(app_core STATIC src/app_core.cpp src/tool_check.cpp)
target_include_directories(app_core PUBLIC src "${CMAKE_BINARY_DIR}/include")
target_link_libraries(app_core PRIVATE shell_parser)
add_executable(ec src/main.cpp)
target_include_directories(ec PRIVATE "${CMAKE_BINARY_DIR}/generated")
target_link_libraries(ec PRIVATE app_core llama)
add_custom_command(TARGET ec POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E rm -f "$<TARGET_FILE_DIR:ec>/easycommand"
    COMMAND ${CMAKE_COMMAND} -E create_symlink ec "$<TARGET_FILE_DIR:ec>/easycommand"
    VERBATIM)
install(TARGETS ec RUNTIME DESTINATION bin)
install(PROGRAMS "${CMAKE_BINARY_DIR}/easycommand" DESTINATION bin)
install(FILES LICENSE THIRD_PARTY.md DESTINATION share/licenses/easycommand)
install(DIRECTORY licenses/ DESTINATION share/licenses/easycommand)

if(BUILD_TESTING)
    find_package(Python3 COMPONENTS Interpreter REQUIRED)
    add_executable(contract-driver tests/contract_driver.cpp)
    target_link_libraries(contract-driver PRIVATE app_core)
    add_executable(tokenizer-probe tests/tokenizer_probe.cpp)
    target_include_directories(tokenizer-probe PRIVATE src "${CMAKE_BINARY_DIR}/generated" "${CMAKE_BINARY_DIR}/include")
    target_link_libraries(tokenizer-probe PRIVATE llama)
    add_test(NAME application-contract
        COMMAND ${Python3_EXECUTABLE} -m unittest discover -s tests -p test_application.py -v)
    set_tests_properties(application-contract PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        ENVIRONMENT "EASYCOMMAND_TEST_DRIVER=$<TARGET_FILE:contract-driver>")
    add_test(NAME installer-collisions
        COMMAND ${Python3_EXECUTABLE} -m unittest discover -s tests -p test_install.py -v)
    set_tests_properties(installer-collisions PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
endif()
