#include <iostream>
#include <vector>
#include <random>
#include <chrono>
#include <numeric>

// 引入 libdivide 头文件
#include "libdivide.h"

// 防止编译器优化掉循环中的计算
volatile uint64_t sink;

int main() {
    // 1. 设置测试规模
    const size_t NUM_OPERATIONS = 100'000'000; // 1亿次操作
    std::vector<uint64_t> random_hashes(NUM_OPERATIONS);

    // 2. 生成随机数据 (模拟 Hash 值)
    std::mt19937_64 rng(42);
    // 生成一个非 2 的幂的随机除数 (模拟 Bloom Filter 的 size)
    // 如果是 2 的幂，编译器会自动优化为位运算，测试就没意义了
    uint64_t divisor = 0;
    while (divisor == 0 || (divisor & (divisor - 1)) == 0) {
        divisor = rng(); 
    }
    
    // 填充随机 Hash 值
    for (size_t i = 0; i < NUM_OPERATIONS; ++i) {
        random_hashes[i] = rng();
    }

    std::cout << "Testing with divisor: " << divisor << std::endl;
    std::cout << "Operations count: " << NUM_OPERATIONS << std::endl;
    std::cout << "------------------------------------------------" << std::endl;

    // ==========================================
    // 测试 1: 标准取模运算 (%)
    // ==========================================
    auto start_native = std::chrono::high_resolution_clock::now();
    
    uint64_t sum_native = 0;
    for (size_t i = 0; i < NUM_OPERATIONS; ++i) {
        // 核心瓶颈：CPU 的 div 指令
        sum_native += random_hashes[i] % divisor;
    }
    sink = sum_native; // 防止被优化

    auto end_native = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> diff_native = end_native - start_native;
    std::cout << "[Native %] Time: " << diff_native.count() << " s" << std::endl;


    // ==========================================
    // 测试 2: libdivide 优化
    // ==========================================
    auto start_libdivide = std::chrono::high_resolution_clock::now();

    // 预计算阶段 (Pre-computation)
    // 这一步计算了 magic number 和 shift 用于替代除法
    libdivide::divider<uint64_t> fast_d(divisor);

    uint64_t sum_libdivide = 0;
    for (size_t i = 0; i < NUM_OPERATIONS; ++i) {
        // libdivide 没有直接的 % 操作符，通常做法是：
        // remainder = numerator - (quotient * divisor)
        // libdivide 的除法变成了乘法+位移
        uint64_t quotient = random_hashes[i] / fast_d;
        uint64_t remainder = random_hashes[i] - (quotient * divisor);
        
        sum_libdivide += remainder;
    }
    sink = sum_libdivide;

    auto end_libdivide = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> diff_libdivide = end_libdivide - start_libdivide;
    std::cout << "[libdivide] Time: " << diff_libdivide.count() << " s" << std::endl;

    // ==========================================
    // 结果对比
    // ==========================================
    std::cout << "------------------------------------------------" << std::endl;
    std::cout << "Speedup: " << diff_native.count() / diff_libdivide.count() << "x" << std::endl;

    if (sum_native != sum_libdivide) {
        std::cerr << "ERROR: Results do not match!" << std::endl;
    }

    return 0;
}
