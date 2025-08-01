#include <iostream>
#include <cstdlib>

struct DynamicArray {
    int* data;
    size_t size;
    size_t capacity;
};

DynamicArray* createDynamicArray(size_t initial_capacity) {
    DynamicArray* array = static_cast<DynamicArray*>(malloc(sizeof(DynamicArray)));
    if (!array) {
        std::cerr << "Memory allocation failed [0]." << std::endl;
        return nullptr;
    }

    array->data = static_cast<int*>(malloc(initial_capacity * sizeof(int)));
    if (!array->data) {
        std::cerr << "Memory allocation failed [1]." << std::endl;
        free(array);
        return nullptr;
    }

    array->capacity = initial_capacity;
    array->size = 0;
    return array;
}

void push_back(DynamicArray* array, int value) {
    if (array->size == array->capacity) {
        array->capacity *= 2;
        int* new_data = static_cast<int*>(realloc(array->data, array->capacity * sizeof(int)));
        if (!new_data) {
            std::cerr << "Memory reallocation failed." << std::endl;
            return;
        }
        std::cout << ">>> do resize\n";
        array->data = new_data;
    } else {
        std::cout << "no resize\n";
    }
    array->data[array->size] = value;
    array->size++;
}

void freeDynamicArray(DynamicArray* array) {
    free(array->data);
    free(array);
}

int main() {
    DynamicArray* array = createDynamicArray(10);
    if (!array) {
        return 1;
    }

    for (int i=0; i<10; ++i) {
        push_back(array, i);
    }

    for (int i=0; i<10; ++i) {
        push_back(array, i);
    }
    
    for (int i=0; i<10; ++i) {
        push_back(array, i);
    }

    freeDynamicArray(array);
    return 0;
}