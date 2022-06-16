#include <iostream>
#include <stdlib.h>
#include <string.h>

int main() {
    const void* p0 = calloc(10, sizeof(int));
    std::cout << p0 << std::endl;

    const int* p1 = static_cast<const int*>(p0);
    std::cout << p1 << std::endl;
    std::cout << p1 + 1 << std::endl;

    memset(p1, 10, 10);
    free(const_cast<void*>(p0));
    return 0;
}