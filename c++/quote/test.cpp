#include <iostream>
#include <typeinfo>

int main() {
    char test1[] = "Hello";
    auto test2 = 'Hello';

    
    std::cout << typeid(test1).name() << std::endl;
    std::cout << typeid(test2).name() << std::endl;

    std::cout << sizeof("Hello") << std::endl;
    std::cout << sizeof('Hello') << std::endl;

    return 0;
}