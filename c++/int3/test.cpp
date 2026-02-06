#include <iostream>

// Inline assembly for int3 breakpoint
#ifdef __GNUC__
    #define DEBUG_BREAK() asm("int3")
#elif _MSC_VER
    #define DEBUG_BREAK() __debugbreak()
#else
    #define DEBUG_BREAK()
#endif

int main() {
    std::cout << "Program started\n";
    
    int x = 42;
    std::cout << "x = " << x << "\n";
    
    // Trigger breakpoint here
    DEBUG_BREAK();
    
    std::cout << "After breakpoint\n";
    
    return 0;
}