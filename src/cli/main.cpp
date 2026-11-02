#include <iostream>
#include <string>
#include <vector>

#include "memoryforge/cli/cli.hpp"

int main(int argc, char** argv) {
    std::vector<std::string> args(argv + 1, argv + argc);
    return memoryforge::run_cli(args, std::cout, std::cerr);
}
