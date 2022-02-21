#ifndef _RVO_
#define _RVO_
#include <iostream>

struct base {
    public:
        base(int a): m_value(a) {
            std::cout << "default constructor\n";
        }
        base(const base& rhs) {
            this->m_value = rhs.m_value;
            std::cout << "copy constructor\n";
        }
        base(base&& rhs) {
            std::swap(this->m_value,rhs.m_value);
            std::cout << "move constructor\n";
        }
    protected:
    private:
        int m_value;
};

base foo(int x) {
    base tmp(x);
    return tmp;
}
int main() {
    base a(10);
    std::cout << "-----------\n";
    auto b = foo(12);
    std::cout << "-----------\n";
    auto c = base(32);
}
#endif // _RVO_