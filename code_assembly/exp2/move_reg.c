#include <stdio.h>

int main(int argc, char* argv[]) {
    int a = 10;
    int b = 20;
    int s0 = 0;
    int s1 = 0;

    __asm__ (
        "mov %2, %0\n\t"
        "mov %3, %1\n\t"
        : "=r"(s0), "=r"(s1)
        : "r"(a), "b"(b)
    );

    printf("s0 = %d\n", s0);
    printf("s1 = %d\n", s1);
    return 0;
}