// Xbyak JIT example: using dump_jit_perf to profile Xbyak-generated code with
// Linux perf.
//
// This example:
//   1. Uses Xbyak to JIT a vector-add kernel (c[i] = a[i] + b[i])
//   2. Registers the generated code with perf via jitdump and perfmap
//   3. Runs the JIT kernel long enough for perf sampling
//
// Build:
//   g++ -O2 -mavx2 -std=c++11 -I../../3rdparty/xbyak -o example example.cpp
//
// Run with perf:
//   perf record -k 1 -g ./example 5
//   perf inject --jit -i perf.data -o perf.jit.data
//   perf report -i perf.jit.data
//
// Or simply run to see the perf artifacts:
//   ./example 1

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "dump_jit_perf.h"
#include "../../3rdparty/xbyak/xbyak/xbyak.h"

static volatile uint64_t g_sink = 0;

// Vector-add kernel: void vadd(const float *a, const float *b, float *c, int64_t n)
//
// System V AMD64 ABI:
//   rdi = a,  rsi = b,  rdx = c,  rcx = n
//
// Strategy:
//   - Main loop: 8 floats per iteration with AVX (256-bit ymm)
//   - Tail loop: remaining floats one at a time
class vadd_kernel_t : public Xbyak::CodeGenerator {
public:
    vadd_kernel_t() {
        Xbyak::Reg64 reg_a = rdi;
        Xbyak::Reg64 reg_b = rsi;
        Xbyak::Reg64 reg_c = rdx;
        Xbyak::Reg64 reg_n = rcx;
        Xbyak::Reg64 reg_i = rax;

        xor_(reg_i, reg_i);  // i = 0

        // avx_end = n & ~7
        Xbyak::Reg64 reg_avx_end = r8;
        mov(reg_avx_end, reg_n);
        and_(reg_avx_end, ~7);

        L(".avx_loop");
        cmp(reg_i, reg_avx_end);
        jge(".tail_check");

        vmovups(ymm0, ptr[reg_a + reg_i * 4]);
        vaddps(ymm0, ymm0, ptr[reg_b + reg_i * 4]);
        vmovups(ptr[reg_c + reg_i * 4], ymm0);

        add(reg_i, 8);
        jmp(".avx_loop");

        L(".tail_check");
        cmp(reg_i, reg_n);
        jge(".done");

        L(".tail_loop");
        vmovss(xmm0, ptr[reg_a + reg_i * 4]);
        vaddss(xmm0, xmm0, ptr[reg_b + reg_i * 4]);
        vmovss(ptr[reg_c + reg_i * 4], xmm0);
        inc(reg_i);
        cmp(reg_i, reg_n);
        jl(".tail_loop");

        L(".done");
        vzeroupper();
        ret();
    }

    using func_t = void (*)(const float *, const float *, float *, int64_t);

    func_t get() const { return getCode<func_t>(); }
    const uint8_t *code() const { return getCode<const uint8_t *>(); }
    size_t code_size() const { return getSize(); }
};

static void run_vadd_workload(vadd_kernel_t::func_t vadd_func,
        float *a, float *b, float *c, int64_t n, int scale) {
    const int iterations = 2000000 * scale;
    for (int i = 0; i < iterations; ++i) {
        vadd_func(a, b, c, n);
    }
    // Prevent dead-code elimination
    double acc = 0;
    for (int64_t i = 0; i < n; ++i) acc += c[i];
    g_sink += static_cast<uint64_t>(acc);
}

static bool verify(const float *a, const float *b, const float *c, int64_t n) {
    for (int64_t i = 0; i < n; i++) {
        float expected = a[i] + b[i];
        if (c[i] != expected) {
            printf("MISMATCH at [%ld]: got %f, expected %f\n",
                    (long)i, c[i], expected);
            return false;
        }
    }
    return true;
}

static double elapsed_ms(const std::chrono::steady_clock::time_point &begin,
        const std::chrono::steady_clock::time_point &end) {
    return std::chrono::duration_cast<std::chrono::duration<double, std::milli>>(
                   end - begin)
            .count();
}

int main(int argc, char **argv) {
    printf("=== JIT Perf Dump Example: Vector Add ===\n\n");

    int scale = 1;
    for (int i = 1; i < argc; ++i) {
        const int parsed = std::atoi(argv[i]);
        if (parsed > 0) scale = parsed;
    }

    const int64_t N = 1024 + 3;  // non-multiple-of-8 to exercise tail loop

    std::vector<float> a(N), b(N), c(N, 0.0f);
    for (int64_t i = 0; i < N; i++) {
        a[i] = static_cast<float>(i);
        b[i] = static_cast<float>(i) * 0.5f;
    }

    printf("pid=%d\n", getpid());
    printf("workload scale=%d, N=%ld\n\n", scale, (long)N);

    vadd_kernel_t kernel;

    // Register with perf
    jit_perf_dump::register_jit_code_linux_perf(
            kernel.code(), kernel.code_size(), "jit_vadd");

    auto vadd_func = kernel.get();

    // Correctness check
    vadd_func(a.data(), b.data(), c.data(), N);
    if (!verify(a.data(), b.data(), c.data(), N)) {
        printf("FAIL: verification failed\n");
        return 1;
    }
    printf("Verification PASSED\n");

    // Benchmark loop (for perf sampling)
    std::fill(c.begin(), c.end(), 0.0f);
    const auto begin = std::chrono::steady_clock::now();
    run_vadd_workload(vadd_func, a.data(), b.data(), c.data(), N, scale);
    const auto end = std::chrono::steady_clock::now();

    printf("[vadd] code=%p size=%zu elapsed=%.3f ms\n",
            static_cast<const void *>(kernel.code()), kernel.code_size(),
            elapsed_ms(begin, end));

    printf("\nsink=%llu\n", static_cast<unsigned long long>(g_sink));
    printf("jitdump: $HOME/.debug/jit/jitperf.*/jit-<pid>.dump\n");
    printf("perfmap: /tmp/perf-<pid>.map\n");

    return 0;
}
