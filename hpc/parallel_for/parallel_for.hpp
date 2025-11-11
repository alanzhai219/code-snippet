template <typename F>
void parallel_nt(int nthr, const F& func) {
    if (nthr == 0) {
        nthr = parallel_get_max_threads();
    }

    if (nthr == 1) {
        func(0, 1);
        return;
    }

    // We expect the number of threads here to be "nthr", so we need to disable dynamic behavior.
    // https://learn.microsoft.com/zh-cn/cpp/parallel/openmp/reference/openmp-functions?view=msvc-170#omp-get-dynamic
    auto origin_dyn_val = omp_get_dynamic();
    if (origin_dyn_val != 0) {
        omp_set_dynamic(0);
    }

#   pragma omp parallel num_threads(nthr)
    { func(parallel_get_thread_num(), parallel_get_num_threads()); }

    if (origin_dyn_val != 0) {
        omp_set_dynamic(origin_dyn_val);
    }
}

template <typename T, typename Q>
inline void splitter(const T& n, const Q& team, const Q& tid, T& n_start, T& n_end) {
    if (team <= 1 || n == 0) {
        n_start = 0;
        n_end = n;
    } else {
        T n1 = (n + (T)team - 1) / (T)team;
        T n2 = n1 - 1;
        T T1 = n - n2 * (T)team;
        n_end = (T)tid < T1 ? n1 : n2;
        n_start = (T)tid <= T1 ? tid * n1 : T1 * n1 + ((T)tid - T1) * n2;
    }

    n_end += n_start;
}
