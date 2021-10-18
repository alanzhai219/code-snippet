#include <iostream>
#include <memory>

class A {
    public:
        virtual void print() = 0;
};

class B : public A {
    public:
        virtual void print() {
            std::cout << "Hello World" << std::endl;
        }

        int m_val = 0;

};

A* get1() {
    return new B;
}

A& get2() {
    static B b;
    return b;
}

std::unique_ptr<A> get3() {
    return std::make_unique<B>();
}

int main(int argc, char* argv) {
    auto a1 = get1();
    a1->print();
    delete a1;

    auto &a2 = get2();
    a2.print();

    auto a3 = get3();
    a3->print();
    return 0;
}