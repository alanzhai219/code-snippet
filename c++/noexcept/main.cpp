#include <iostream>
#include <vector>

using namespace std;

class A {
  public:
    A(int value) {
    }

    A(const A &other) {
        std::cout << "copy constructor\n";
    }

    // if remove noexcept, will call copy constructor.
    // Otherwise, move constructor will be selected.
    A(A &&other) noexcept {
        std::cout << "move constructor\n";
    }
};

int main() {
    std::vector<A> a;
    a.emplace_back(1);
    a.emplace_back(2);

    return 0;
}
