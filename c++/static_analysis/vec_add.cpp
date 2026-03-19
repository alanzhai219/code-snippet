#include <iostream>

float vec_sum(float* vec, int size) {
    float sum = 0.0f;
    for (int i = 0; i < size; ++i) {
        sum += vec[i];
    }
    return sum;
}

float vec_multiply_sum(float* vec, int size) {
    float sum = 0.0f;
    for (int i = 0; i < size; ++i) {
        sum += vec[i] * 2.0f;
    }
    return sum;
}

int main() {
    float vec[] = {1.0f, 2.0f, 3.0f, 4.0f, 5.0f};
    int size = sizeof(vec) / sizeof(vec[0]);
    float result = vec_sum(vec, size);
    std::cout << "Sum: " << result << std::endl;
    result = vec_multiply_sum(vec, size);
    std::cout << "Multiply Sum: " << result << std::endl;
    return 0;
}