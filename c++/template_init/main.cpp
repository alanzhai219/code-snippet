#include <iostream>
#include "ArrayRef.h"

namespace at {
    using namespace c10;
    
    enum ScalarType {
        Float=0,
        Half,
        Int,
        Long,
        Double
    };
}

int main() {
    at::ArrayRef<at::ScalarType> types = {at::ScalarType::Float,
                                          at::ScalarType::Int};
    std::cout << types[0] << "\n";
    std::cout << types[1] << "\n";
    std::cout << "=========================\n";

    std::initializer_list<at::ScalarType> types_vec = {at::ScalarType::Float,
                                                       at::ScalarType::Int};
    at::ArrayRef<at::ScalarType> types2(types_vec);
    std::cout << types2[0] << "\n";
    std::cout << types2[1] << "\n";
    return 0;
}
