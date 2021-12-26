#include <iostream>

class Base {
    public:
        Base(const& Base) = delete;
        Base& operator=(const& Base) = delete;
    
    public:
        void print() {
            std::cout << "This is a test\n";
        }
};
int main() {
    Base a;
    a.print();
    return 0;
}