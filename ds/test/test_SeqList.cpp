#include <iostream>

#include "../include/StaticList.hpp"

int main() {
    DS::StaticList<int, 8> sl;
    sl.insert(0, 10);
    sl.insert(1, 100);
    sl.insert(2, 1000);

    int val = 0;
    auto ret = sl.get(1, val);
    std::cout << val << std::endl;

    return 0;
}