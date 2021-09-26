#include <stdio.h>

extern int add(int a, int b);

int main(int argc, char* argv[]) {
    int i = 0;
    i = add(5,6);
    printf("%d\n", i);
    return 0;
}