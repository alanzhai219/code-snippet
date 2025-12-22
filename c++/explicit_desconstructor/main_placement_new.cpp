#include <iostream>
#include <string>
#include <new> // 必须包含，为了使用 placement new

struct Widget {
    Widget(int i) : s(i) {
        ptr = malloc(i);
        std::cout << "Widget Constructor, alloc size = " << s << std::endl;
    }
    ~Widget() {
        free(ptr); 
        std::cout << "~Widget Deconstructor, size = " << s << std::endl;
    }

    int s;
    void *ptr;
};

int main() {
    void* buf = malloc(sizeof(Widget));
    
    if (!buf) {
        throw std::runtime_error("Allocate memory failed!!!");
    }

    // placement new
    // syntax:
    // new (address) Type(constructor_arguments);
    Widget* w = new(buf) Widget(123);

    std::cout << "widget - alloc - " << w->s << std::endl;
        
    w->~Widget();

    free(buf);
    return 0;    
}