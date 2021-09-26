#include <stdio.h>

#define GREATER(a, b, result) ({    \
    __asm__(                        \
        "cmp %1, %2     \n\t"       \
        "jge 0f         \n\t"       \
        "movl %1, %0    \n\t"       \
        "jmp 1f         \n\t"       \
        "0:             \n\t"       \
        "movl %2, %0    \n\t"       \
        "1:             \n\t"       \
        :"=r"(result)               \   // %0 
        :"r"(a), "r"(b)             \   // %1, %2
        );                          \
})

int main(int argc, char* argv[]) {
    int data1 = 10;
    int data2 = 20;

    int result = 0;

    GREATER(data1, data2, result);
    printf("a=%d, b=%d, result=%d\n", data1, data2, result);

    data1 = 30;
    GREATER(data1, data2, result);
    printf("a=%d, b=%d, result=%d\n", data1, data2, result);
    return 0;
}