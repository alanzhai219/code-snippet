#include <array>

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
    T end_idx = start + chunk + (tid < remain);
    return {beg_idx, end_idx};
}