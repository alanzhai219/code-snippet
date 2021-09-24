#include <stdio.h>

void main() {
    int a=10, b;
    asm(
        "movl %1, %%eax;"
        "movl %%eax, %0;"
        :"=r"(b)    // output
        :"r"(a)     // input
        :"%eax"     // broken reg
    );

    printf("%d",b);
}
