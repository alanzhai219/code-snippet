#include <stdio.h>

int callee(int arg1,
           int arg2,
           int arg3,
           int arg4,
           int arg5,
           int arg6,
           int arg7,
           int arg8) {
    return arg7 + arg8;
}

int main() {
    int a = 7;
    int b = 8;
    callee(1,2,3,4,5,6,a,b);
    return 0;
}