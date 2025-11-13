#include <array>
#include <omp.h>
#include <vector>
#include <chrono>
#include <iostream>

class ompDynGuard {
    ompDynGuard() {
        origin_dyn_val = omp_get_dynamic();
        if (origin_dyn_val) { omp_set_dynamic(0); }
    }
    ~ompDynGuard() {
        if (origin_dyn_val) {
            omp_set_dynamic(origin_dyn_val);
        }
    }

private:
    int origin_dyn_val;
};

template <typename F>
void parallel_nt(int nthr, const F& func) {
    if (nthr <= 1) {
        func(0, 1);
        return;
    }
    
    ompDynGuard guard;

    // disable dynamic if possible
#pragma omp parallel num_threads(nthr)
    {
        const int tid = omp_get_thread_num();
        const int nthr_act = omp_get_num_threads();
        func(tid, nthr_act);
    }
}

/*
template <typename F>
void parallel_nt(int nthr, const F& func) {
    if (nthr <= 1) {
        func(0, 1);
        return;
    }
    
    // disable dynamic if possible
    const int origin_dyn_val = omp_get_dynamic();
    if (origin_dyn_val) { omp_set_dynamic(0); }

#pragma omp parallel num_threads(nthr)
    {
        const int tid = omp_get_thread_num();
        const int nthr_act = omp_get_num_threads();
        func(tid, nthr_act);
    }

    // enable dynamic if possible
    if (origin_dyn_val) { omp_set_dynamic(origin_dyn_val); }
}
*/

template <typename T>
std::array<T, 2> splitter(T n, T team, T tid) {
    if (team <= 1 || n == 0) {
        return {0, n};
    }

    // split by average. each chunk's num is almost same.
    // first remain chunk get extra 1 element.
    T chunk = n / team;
    T remain = n % team;
    
    T beg_idx = tid * chunk + std::min(tid, remain);
    T end_idx = beg_idx + chunk + (tid < remain);
    return {beg_idx, end_idx};
}

void vector_add(const float* A, const float* B, float* C, size_t N, int nthr = 0) {
    parallel_nt(nthr, [&](size_t ithr, size_t nthr_act) {
        auto start_end_vec = splitter(N, nthr_act, ithr);
        size_t start = start_end_vec[0];
        size_t end = start_end_vec[1];

        // 主循环（可选手动向量化提示, 告诉编译期生成可向量化的代码）
#pragma omp simd
        for (size_t i = start; i < end; ++i) {
            C[i] = A[i] + B[i];
        }
    });
}

int main() {
    const size_t N = 1 << 24;
    std::vector<float> A(N, 1.5f), B(N, 2.0f), C(N, 0.0f);

    auto t0 = std::chrono::high_resolution_clock::now();
    vector_add(A.data(), B.data(), C.data(), N);
    auto t1 = std::chrono::high_resolution_clock::now();

    // 校验结果
    for (int i = 0; i < 10; ++i) {
        std::cout << C[i] << " ";
    }
    std::cout << "\n耗时: "
              << std::chrono::duration<double>(t1 - t0).count()
              << " 秒\n";
    return 0;
}