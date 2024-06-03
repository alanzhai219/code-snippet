//clang 6.0.0

#include <iostream>
#include <array>
#include <cstdint>

namespace SIMD_ENS {
    enum class lsc_data_size { default_size };
    enum class cache_hint { cached };

    template<typename T, int N, lsc_data_size DS, cache_hint L1, cache_hint L2>
    std::array<T, N> lsc_block_load(const T* ptr) {
        std::array<T, N> data;
        for (int i = 0; i < N; ++i) {
            data[i] = ptr[i];
        }
        return data;
    }
}

template<typename T, int N>
class SimdVector {
public:
    std::array<T, N> data;

    template<typename U>
    SimdVector<U, N>& bit_cast_view() {
        return reinterpret_cast<SimdVector<U, N>&>(*this);
    }

    template<int M, int Stride>
    std::array<T, M>& select(int offset) {
        return *reinterpret_cast<std::array<T, M>*>(data.data() + offset);
    }
};

int main() {
    const size_t size = 256;
    uint8_t data[size];

    for (size_t i = 0; i < size; ++i) {
        data[i] = i;
    }

    SimdVector<unsigned char, 256> aaa;

    aaa.template bit_cast_view<unsigned char>().template select<256, 1>(0) =
        SIMD_ENS::lsc_block_load<
        uint8_t,
        256,
        SIMD_ENS::lsc_data_size::default_size,
        SIMD_ENS::cache_hint::cached,
        SIMD_ENS::cache_hint::cached>(data);

    for (size_t i = 0; i < size; ++i) {
        std::cout << static_cast<int>(aaa.data[i]) << " ";
    }
    std::cout << std::endl;

    return 0;
}

