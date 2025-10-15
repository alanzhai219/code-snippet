#include <iostream>
#include <map>
#include <unordered_map>
#include <vector>
#include <algorithm>
#include <chrono>

int main() {
    const int N = 100; // 小数据集
    const int ITERATIONS = 10000; // 重复查找以放大缓存效应
    std::map<int, int> m;
    std::unordered_map<int, int> um;
    std::vector<std::pair<int, int>> vec;

    // 填充数据
    for (int i = 0; i < N; ++i) {
        m[i] = i;
        um[i] = i;
        vec.emplace_back(i, i);
    }
    // 对 vector 排序以支持二分查找
    std::sort(vec.begin(), vec.end());
    
     // 测试 std::map 查找
    auto start = std::chrono::high_resolution_clock::now();
    for (int iter = 0; iter < ITERATIONS; ++iter) {
        for (int i = 0; i < N; ++i) {
            volatile int val = m.find(i)->second; // volatile 防止优化
        }
    }
    auto end = std::chrono::high_resolution_clock::now();
    auto map_time = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
    std::cout << "map: " << map_time / (N * ITERATIONS) << " ns per lookup\n";

    // 测试 unordered_map 查找
    start = std::chrono::high_resolution_clock::now();
    for (int iter = 0; iter < ITERATIONS; ++iter) {
        for (int i = 0; i < N; ++i) {
            volatile int val = um.find(i)->second; // volatile 防止优化
        }
    }
    end = std::chrono::high_resolution_clock::now();
    auto um_time = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
    std::cout << "unordered_map: " << um_time / (N * ITERATIONS) << " ns per lookup\n";

    // 测试 vector 二分查找
    start = std::chrono::high_resolution_clock::now();
    for (int iter = 0; iter < ITERATIONS; ++iter) {
        for (int i = 0; i < N; ++i) {
            auto it = std::lower_bound(vec.begin(), vec.end(), std::make_pair(i, 0),
                                       [](const auto& p, const auto& v) { return p.first < v.first; });
            volatile int val = it->second; // volatile 防止优化
        }
    }
    end = std::chrono::high_resolution_clock::now();
    auto vec_time = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
    std::cout << "vector (binary search): " << vec_time / (N * ITERATIONS) << " ns per lookup\n";

    return 0;
}
