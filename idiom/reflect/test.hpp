#ifndef _TEST_HPP_
#define _TEST_HPP_

#include <iostream>

class Test {
    public:
        Test() {
            std::cout << "call Test Constructor\n";
        }

        virtual ~Test() {
            std::cout << "call Test DeConstructor\n";
        }

        void print() {
            std::cout << "call Test Print func\n";
        }
};

void* create_Test() {
    Test *t = new Test;
    return t;
}
#endif // _TEST_HPP_