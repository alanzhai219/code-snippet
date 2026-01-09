#include <iostream>
#include <vector>
#include <random>
#include <type_traits>
#include <cstring>

#include <immintrin.h>

class RandomGenerator {
public:
    // 1. 生成整数 [min, max] (闭区间)
    static int getInt(int min, int max) {
        return getIntDist(min, max)(getEngine());
    }

    // 2. 生成浮点数 [min, max) (左闭右开)
    static float getFloat(float min, float max) {
        return getFloatDist(min, max)(getEngine());
    }

    // 泛型版本 (支持 double, long 等)
    template<typename T>
    static T get(T min, T max) {
        if constexpr (std::is_integral_v<T>) {
            std::uniform_int_distribution<T> dist(min, max);
            return dist(getEngine());
        } else {
            std::uniform_real_distribution<T> dist(min, max);
            return dist(getEngine());
        }
    }

private:
    // 获取线程局部唯一的生成器
    static std::mt19937& getEngine() {
        // random_device 用于生成种子 (非确定性随机数，通常基于硬件)
        static thread_local std::random_device rd;
        // mt19937 是标准的 Mersenne Twister 算法，速度快且质量高
        static thread_local std::mt19937 generator(rd());
        return generator;
    }

    // 辅助函数：构造分布对象
    // 注意：分布对象很轻量，可以每次创建，也可以缓存，视需求而定
    static std::uniform_int_distribution<int> getIntDist(int min, int max) {
        return std::uniform_int_distribution<int>(min, max);
    }

    static std::uniform_real_distribution<float> getFloatDist(float min, float max) {
        return std::uniform_real_distribution<float>(min, max);
    }
};

template <typename T>
void print_range(const std::vector<T>& values, size_t len, const std::string& ext = "") {
    if (!ext.empty()) {
        std::cout << ext << "\n";
    }

    size_t dump_len = std::min(values.size(), len);
    for (size_t i = 0; i < dump_len; ++i) {
        std::cout << values[i] << " ";
    }
    std::cout << std::endl;
}

void load_store_test_1() {
    std::vector<float> data(1024);
    for (auto& x : data) {
        x = RandomGenerator::getFloat(0.f, 10.f);
    }

    // ld mem to reg
    __m512 reg_zmm = _mm512_loadu_ps(data.data());

    // st reg to mem
    std::vector<float> data2(64, 0.f);
    _mm512_storeu_ps(data2.data(), reg_zmm);

    // check
    print_range(data, 16, "data");
    print_range(data2, 16, "data2");
}

void load_store_test_2() {
    std::vector<float> data(1024);
    for (auto& x : data) {
        x = RandomGenerator::getFloat(0.f, 10.f);
    }

    // ld mem to reg
    __m512 reg_zmm = _mm512_loadu_ps(data.data());

    // st reg to mem
    std::vector<float> data2(64, 0.f);
    float* fp_data = reinterpret_cast<float*>(&reg_zmm);
    memcpy(data2.data(), fp_data, sizeof(__m512));

    // check
    print_range(data, 16, "data");
    print_range(data2, 16, "data2");
}

int main() {
    load_store_test_1();
    load_store_test_2();
    return 0;
}