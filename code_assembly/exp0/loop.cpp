#include <ctime>

#include <iostream>

#define TIMES 100000

void calcuC(int *x,int *y,int length) {
    for(int i = 0; i < TIMES; i++) {
        for(int j = 0; j < length; j++) {
            x[j] += y[j];
        }
    }
}

void calcuAsm(int *x,int *y,int lengthOfArray) {
    __asm__ (
       "mov edi,TIMES
        start: 
        mov esi,0
        mov ecx,lengthOfArray
        label:
        mov edx,x
        push edx
        mov eax,DWORD PTR [edx + esi*4]
        mov edx,y
        mov ebx,DWORD PTR [edx + esi*4]
        add eax,ebx
        pop edx
        mov [edx + esi*4],eax
        inc esi
        loop label
        dec edi
        cmp edi,0
        jnz start"
    );
}

int main(int argc, char* argv[]) {
    bool errorOccured = false;
    setbuf(stdout,NULL);
    int *xC,*xAsm,*yC,*yAsm;
    xC = new int[2000];
    xAsm = new int[2000];
    yC = new int[2000];
    yAsm = new int[2000];
    for(int i = 0; i < 2000; i++) {
        xC[i] = 0;
        xAsm[i] = 0;
        yC[i] = i;
        yAsm[i] = i;
    }
    time_t start = clock();
    calcuC(xC,yC,2000);
    time_t end = clock();
    std::cout<<"time = "<<(float)(end - start) / CLOCKS_PER_SEC<<"\n";


    start = clock();
    calcuAsm(xAsm,yAsm,2000);
    end = clock();
    std::cout<<"time = "<<(float)(end - start) / CLOCKS_PER_SEC<<"\n";

    for(int i = 0; i < 2000; i++) {
        if(xC[i] != xAsm[i]) {
            std::cout<<"xC["<<i<<"]="<<xC[i]<<" "<<"xAsm["<<i<<"]="<<xAsm[i]<<std::endl;
            errorOccured = true;
            break;
        }
    }
    if(errorOccured)
        std::cout<<"Error occurs!\n";
    else
        std::cout<<"Works fine!\n";

    return 0;
}