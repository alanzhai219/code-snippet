#include <iostream>
#include <string>
#include <typeinfo>
#include <typeindex>
#include <cassert>

struct Base {}; // non-polymorphic
struct Derived : Base {};

struct Base2 { virtual void foo() {} }; // polymorphic
struct Derived2 : Base2 {};

int main() {
    Derived d1;
    Base& b1 = d1;
    std::cout << "reference to non-polymorphic base: " << typeid(b1).name() << '\n';

    const std::type_info& ti1 = typeid(b1);
    const std::type_info& ti2 = typeid(b1);

    assert(&ti1 == &ti2); // not guaranteed
    assert(ti1.hash_code() == ti2.hash_code()); // guaranteed
    assert(std::type_index(ti1) == std::type_index(ti2)); // guaranteed

    Derived2 d2;
    Base2& b2 = d2;
    std::cout << "reference to polymorphic base: " << typeid(b2).name() << '\n';
    const std::type_info& ti3 = typeid(b2);
    const std::type_info& ti4 = typeid(b2);

    assert(&ti3 == &ti4); // not guaranteed
    assert(ti3.hash_code() == ti4.hash_code()); // guaranteed
    assert(std::type_index(ti3) == std::type_index(ti4)); // guaranteed
    return 0;
}
