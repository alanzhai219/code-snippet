#include "registry.hpp"
#include <iostream>

REGISTER_OP(add_op, add)
    .describe("Do elementwise add");

REGISTER_OP(sub_op, sub)
    .describe("Do substraction on inputs")
    .set_num_inputs(2);

int main() {
    std::cout << "main is called" << std::endl;
    PRINT_ALL_OPS;
    std::cout << "main is returned" << std::endl;
    return 0;
} 