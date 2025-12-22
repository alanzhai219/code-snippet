#include <iostream>
#include <string>
#include <new> // 必须包含，为了使用 placement new
#include <memory>

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
    Widget* w = reinterpret_cast<Widget*>(buf);
    std::construct_at(w, 123);

    std::cout << "widget - alloc - " << w->s << std::endl;
        
    std::destroy_at(w); 

    free(buf);
    return 0;    
}