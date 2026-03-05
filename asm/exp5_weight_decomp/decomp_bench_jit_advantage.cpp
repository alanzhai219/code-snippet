// decomp_bench_jit_advantage.cpp
//
// Benchmark demonstrating the scenario where JIT OUTPERFORMS AVX-512 intrinsics.
//
// Key insight: In LLM inference, weight sparsity masks are FIXED after model
// loading. A JIT kernel can inspect actual mask values at compile-time and
// specialize the generated code. This eliminates ALL runtime address computation
// that generic kernels (both intrinsics and generic JIT) must pay.
//
// What the specialized JIT does differently:
//   1. Pre-computes ALL popcnt values → embeds as immediate add constants
//   2. Pre-computes ALL source offsets → uses [base + IMM] addressing
//   3. Fully unrolls for known block count → zero loop overhead
//   4. Embeds mask values as 64-bit immediates → no bitmask memory loads
//   5. Specializes code paths: skip vpexpandb for zero/dense chunks
//
// Build:
//   g++ decomp_bench_jit_advantage.cpp -I../../3rdparty/xbyak -march=native -O2 -o decomp_bench_jit_advantage
//
// Run:
//   ./decomp_bench_jit_advantage

#include <iostream>
#include <vector>
#include <cstdint>
#include <cstring>
#include <chrono>
#include <cstdlib>
#include <iomanip>

#include "decomp_kernel_avx512_opt.hpp"
#include "decomp_kernel_jit_opt.hpp"

// =================================================================
// Utility functions
// =================================================================

void gen_random_sparse_data(std::vector<uint8_t>& data, size_t size, int sparsity_pct) {
    srand(42);
    for (size_t i = 0; i < size; ++i) {
        if (rand() % 100 < sparsity_pct) {
            data[i] = 0;
        } else {
            data[i] = (uint8_t)((rand() % 255) + 1);
        }
    }
}

void gen_structured_sparse_data(std::vector<uint8_t>& data, size_t size) {
    // Structured sparsity: some chunks are entirely zero, some entirely dense.
    // This is realistic for channel-pruned or block-sparse LLM weights.
    srand(42);
    size_t total_chunks = size / 64;
    for (size_t c = 0; c < total_chunks; ++c) {
        uint8_t* chunk = data.data() + c * 64;
        int r = rand() % 10;
        if (r < 3) {
            // 30% entirely zero chunks
            memset(chunk, 0, 64);
        } else if (r < 4) {
            // 10% entirely dense chunks (all non-zero)
            for (int i = 0; i < 64; ++i)
                chunk[i] = (uint8_t)((rand() % 255) + 1);
        } else {
            // 60% random ~50% sparsity
            for (int i = 0; i < 64; ++i) {
                if (rand() % 2)
                    chunk[i] = (uint8_t)((rand() % 255) + 1);
                else
                    chunk[i] = 0;
            }
        }
    }
}

void compress_weights_local(
    std::vector<uint8_t>& compressed_output,
    std::vector<uint64_t>& bitmask_output,
    const std::vector<uint8_t>& uncompressed_input,
    int num_blocks, int chunks_per_block) {

    compressed_output.clear();
    bitmask_output.resize(num_blocks * chunks_per_block);

    for (int block = 0; block < num_blocks; ++block) {
        for (int cl = 0; cl < chunks_per_block; ++cl) {
            size_t chunk_idx = (size_t)block * chunks_per_block + cl;
            const uint8_t* src = &uncompressed_input[chunk_idx * 64];
            uint64_t mask = 0;
            for (int b = 0; b < 64; ++b) {
                if (src[b] != 0) {
                    mask |= (1ULL << b);
                    compressed_output.push_back(src[b]);
                }
            }
            bitmask_output[chunk_idx] = mask;
        }
        // Cacheline alignment padding
        size_t current_size = compressed_output.size();
        size_t misalignment = current_size & 0x3F;
        if (misalignment != 0) {
            size_t padding = 64 - misalignment;
            for (size_t p = 0; p < padding; ++p)
                compressed_output.push_back(0xDD);
        }
    }
    // Safety padding for over-reads
    compressed_output.resize(compressed_output.size() + 128, 0xDD);
}

// =================================================================
// Mask-Specialized JIT Kernel
// =================================================================
//
// This kernel receives bitmask data at JIT-COMPILE TIME and pre-computes
// every address offset. At runtime, it only needs:
//   - compressed_buf pointer (base for all loads)
//   - decomp_buf pointer (base for all stores)
//
// ELIMINATED at runtime (vs generic kernels):
//   - ALL popcnt instructions (offsets are immediate constants)
//   - ALL prefix-sum / pointer-advance adds
//   - ALL bitmask memory loads (masks embedded as 64-bit immediates)
//   - ALL loop overhead (fully unrolled)
//   - vpexpandb for zero-mask chunks (replaced with zero-store)
//   - vpexpandb for dense chunks (replaced with plain copy)

struct jit_specialized_params_t {
    const void* compressed_buf;  // offset 0
    void* decomp_buf;            // offset 8
};

class jit_decompress_specialized_t : public Xbyak::CodeGenerator {
public:
    Xbyak::Reg64 param1 = rdi;
    Xbyak::Reg64 reg_src = r8;    // compressed base, NEVER modified at runtime
    Xbyak::Reg64 reg_dst = r9;    // decomp base, NEVER modified at runtime
    Xbyak::Reg64 reg_tmp = rax;   // temp for mask immediates

    int blocks_;
    const uint64_t* bitmask_;

    // Stats populated during code generation
    int zero_chunks_ = 0;
    int full_chunks_ = 0;
    int partial_chunks_ = 0;
    size_t code_size_ = 0;

    jit_decompress_specialized_t(int blocks, const uint64_t* bitmask)
        : Xbyak::CodeGenerator(4096 * 256),
          blocks_(blocks), bitmask_(bitmask) {
        generate();
    }

    void generate() {
        // Load data pointers from params struct
        mov(reg_src, ptr[param1 + offsetof(jit_specialized_params_t, compressed_buf)]);
        mov(reg_dst, ptr[param1 + offsetof(jit_specialized_params_t, decomp_buf)]);

        // Track compressed source offset entirely at JIT-compile time.
        // This is the key optimization: src_off is a C++ variable computed
        // during code generation, NOT a runtime register. All addresses
        // become [reg_src + IMMEDIATE].
        int src_off = 0;

        for (int block = 0; block < blocks_; ++block) {
            int bm_base = block * 64;
            int dst_base = block * 4096;

            for (int chunk = 0; chunk < 64; ++chunk) {
                uint64_t mask = bitmask_[bm_base + chunk];
                int popcnt = __builtin_popcountll(mask);
                int dst_off = dst_base + chunk * 64;

                // Rotate ZMM and K registers for ILP
                Xbyak::Zmm zr = Xbyak::Zmm(chunk & 3);
                Xbyak::Opmask kr = Xbyak::Opmask(1 + (chunk & 3));

                if (mask == 0) {
                    // ---- ZERO CHUNK: no data to load ----
                    // 2 instructions instead of 4 (skip mov+kmovq+vpexpandb)
                    vpxord(zr, zr, zr);
                    zero_chunks_++;
                } else if (mask == 0xFFFFFFFFFFFFFFFFULL) {
                    // ---- DENSE CHUNK: plain copy, no expand needed ----
                    // 2 instructions instead of 4 (skip mov+kmovq, use vmovdqu8)
                    vmovdqu8(zr, ptr[reg_src + src_off]);
                    full_chunks_++;
                } else {
                    // ---- PARTIAL CHUNK: embed mask, use static offset ----
                    // 4 instructions, but NO popcnt/add on critical path
                    mov(reg_tmp, mask);        // 64-bit immediate
                    kmovq(kr, reg_tmp);
                    vpexpandb(zr | kr | T_z, ptr[reg_src + src_off]);
                    partial_chunks_++;
                }
                vmovdqu8(ptr[reg_dst + dst_off], zr);

                src_off += popcnt;  // Advance at JIT-compile time, NOT runtime!
            }

            // Inter-block alignment (also pre-computed at JIT-compile time)
            int misalign = src_off & 0x3F;
            if (misalign) src_off += (64 - misalign);
        }

        ret();
        code_size_ = getSize();
    }
};

// =================================================================
// Benchmark runner for one scenario
// =================================================================

void run_scenario(const char* scenario_name,
                  std::vector<uint8_t>& original_data,
                  int num_blocks,
                  int iterations) {
    const int chunks_per_block = 64;
    const int total_chunks = num_blocks * chunks_per_block;
    const size_t output_size = (size_t)num_blocks * 4096;
    const int warmup = 2000;

    std::cout << "\n========================================" << std::endl;
    std::cout << "  " << scenario_name << std::endl;
    std::cout << "  blocks=" << num_blocks
              << ", output=" << output_size << " bytes"
              << ", iterations=" << iterations << std::endl;
    std::cout << "========================================" << std::endl;

    // --- Compress ---
    std::vector<uint8_t> compressed;
    std::vector<uint64_t> bitmask(total_chunks);
    compress_weights_local(compressed, bitmask, original_data, num_blocks, chunks_per_block);

    size_t total_nonzero = 0;
    for (auto m : bitmask) total_nonzero += __builtin_popcountll(m);
    double density = (double)total_nonzero / (total_chunks * 64) * 100;
    std::cout << "  Density: " << std::fixed << std::setprecision(1)
              << density << "%" << std::endl;

    // --- Allocate output buffers ---
    uint8_t* out_avx     = (uint8_t*)aligned_alloc(64, output_size);
    uint8_t* out_jit_gen = (uint8_t*)aligned_alloc(64, output_size);
    uint8_t* out_jit_spec = (uint8_t*)aligned_alloc(64, output_size);

    // --- Create kernels ---
    // 1. AVX-512 intrinsics (generic, compiled code)
    // 2. JIT generic (prefix-sum, runtime popcnt)
    jit_decompress_kernel_t_opt jit_gen(num_blocks);
    auto* jit_gen_func = jit_gen.getCode<void (*)(jit_decomp_params_t_opt*)>();
    jit_gen.ready();

    jit_decomp_params_t_opt gen_args;
    gen_args.compressed_buf = compressed.data();
    gen_args.bitmask_ptr = bitmask.data();
    gen_args.decomp_buf = out_jit_gen;

    // 3. JIT specialized (mask-aware, all offsets pre-computed)
    jit_decompress_specialized_t jit_spec(num_blocks, bitmask.data());
    auto* jit_spec_func = jit_spec.getCode<void (*)(jit_specialized_params_t*)>();
    jit_spec.ready();

    jit_specialized_params_t spec_args;
    spec_args.compressed_buf = compressed.data();
    spec_args.decomp_buf = out_jit_spec;

    // --- Print specialization stats ---
    int total_ch = jit_spec.zero_chunks_ + jit_spec.full_chunks_ + jit_spec.partial_chunks_;
    std::cout << "\n  Specialized JIT Code Generation Stats:" << std::endl;
    std::cout << "    Zero-mask chunks:  " << jit_spec.zero_chunks_ << "/" << total_ch
              << " (" << std::setprecision(1) << (100.0 * jit_spec.zero_chunks_ / total_ch) << "%)"
              << "  → vpxord + store (2 insns)" << std::endl;
    std::cout << "    Full-mask chunks:  " << jit_spec.full_chunks_ << "/" << total_ch
              << " (" << std::setprecision(1) << (100.0 * jit_spec.full_chunks_ / total_ch) << "%)"
              << "  → vmovdqu8 copy (2 insns)" << std::endl;
    std::cout << "    Partial chunks:    " << jit_spec.partial_chunks_ << "/" << total_ch
              << " (" << std::setprecision(1) << (100.0 * jit_spec.partial_chunks_ / total_ch) << "%)"
              << "  → mov+kmovq+vpexpandb+store (4 insns)" << std::endl;
    std::cout << "    Generated code:    " << jit_spec.code_size_ << " bytes" << std::endl;

    // --- Correctness verification ---
    auto verify = [&](const char* name, uint8_t* buf) -> bool {
        if (memcmp(buf, original_data.data(), output_size) != 0) {
            std::cerr << "  FAIL: " << name << std::endl;
            // Print first mismatch
            for (size_t i = 0; i < output_size; ++i) {
                if (buf[i] != original_data[i]) {
                    std::cerr << "    First mismatch at byte " << i
                              << ": got 0x" << std::hex << (int)buf[i]
                              << " expected 0x" << (int)original_data[i]
                              << std::dec << std::endl;
                    break;
                }
            }
            return false;
        }
        std::cout << "  PASS: " << name << std::endl;
        return true;
    };

    std::cout << "\n  Correctness:" << std::endl;
    memset(out_avx, 0, output_size);
    decompress_avx512_opt(out_avx, compressed.data(), bitmask.data(), num_blocks);
    bool ok1 = verify("AVX-512 Intrinsics (generic)", out_avx);

    memset(out_jit_gen, 0, output_size);
    jit_gen_func(&gen_args);
    bool ok2 = verify("JIT Generic (prefix-sum)", out_jit_gen);

    memset(out_jit_spec, 0, output_size);
    jit_spec_func(&spec_args);
    bool ok3 = verify("JIT Specialized (mask-aware)", out_jit_spec);

    if (!ok1 || !ok2 || !ok3) {
        std::cerr << "  *** ABORTING scenario due to verification failure ***" << std::endl;
        free(out_avx); free(out_jit_gen); free(out_jit_spec);
        return;
    }

    // --- Warmup ---
    for (int i = 0; i < warmup; ++i) {
        decompress_avx512_opt(out_avx, compressed.data(), bitmask.data(), num_blocks);
        jit_gen_func(&gen_args);
        jit_spec_func(&spec_args);
    }

    // --- Benchmark ---
    volatile uint8_t sink = 0;

    auto t0 = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < iterations; ++i)
        decompress_avx512_opt(out_avx, compressed.data(), bitmask.data(), num_blocks);
    auto t1 = std::chrono::high_resolution_clock::now();
    sink = out_avx[0];
    double time_avx = std::chrono::duration<double, std::micro>(t1 - t0).count() / iterations;

    t0 = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < iterations; ++i)
        jit_gen_func(&gen_args);
    t1 = std::chrono::high_resolution_clock::now();
    sink = out_jit_gen[0];
    double time_jit_gen = std::chrono::duration<double, std::micro>(t1 - t0).count() / iterations;

    t0 = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < iterations; ++i)
        jit_spec_func(&spec_args);
    t1 = std::chrono::high_resolution_clock::now();
    sink = out_jit_spec[0];
    double time_jit_spec = std::chrono::duration<double, std::micro>(t1 - t0).count() / iterations;

    // --- Results ---
    std::cout << "\n  Performance (avg over " << iterations << " iterations):" << std::endl;
    std::cout << std::fixed << std::setprecision(3);
    const int w = 35;

    std::cout << "    " << std::setw(w) << std::left << "AVX-512 Intrinsics (generic)"
              << ": " << std::setw(8) << std::right << time_avx  << " us/call" << std::endl;
    std::cout << "    " << std::setw(w) << std::left << "JIT Generic (prefix-sum)"
              << ": " << std::setw(8) << std::right << time_jit_gen << " us/call"
              << "  (" << std::setprecision(2) << (time_avx / time_jit_gen)
              << "x vs intrinsics)" << std::setprecision(3) << std::endl;
    std::cout << "    " << std::setw(w) << std::left << "JIT Specialized (mask-aware)"
              << ": " << std::setw(8) << std::right << time_jit_spec << " us/call"
              << "  (" << std::setprecision(2) << (time_avx / time_jit_spec)
              << "x vs intrinsics) ★" << std::setprecision(3) << std::endl;

    std::cout << std::setprecision(2);
    std::cout << "\n  Summary:" << std::endl;
    std::cout << "    JIT Specialized vs Intrinsics:   "
              << (time_avx / time_jit_spec) << "x faster" << std::endl;
    std::cout << "    JIT Specialized vs JIT Generic:   "
              << (time_jit_gen / time_jit_spec) << "x faster" << std::endl;

    (void)sink;
    free(out_avx);
    free(out_jit_gen);
    free(out_jit_spec);
}

// =================================================================
// Main
// =================================================================

int main() {
    std::cout << "==================================================================" << std::endl;
    std::cout << "  When Does JIT Outperform AVX-512 Intrinsics?" << std::endl;
    std::cout << "==================================================================" << std::endl;
    std::cout << std::endl;
    std::cout << "  Real-world scenario: LLM weight decompression during inference." << std::endl;
    std::cout << "  Weight sparsity masks are FIXED after model loading." << std::endl;
    std::cout << "  A JIT kernel can inspect actual mask values at compile-time" << std::endl;
    std::cout << "  and eliminate ALL runtime address computation overhead." << std::endl;

    // ---- Scenario A: Random 70% sparsity ----
    // Typical unstructured pruning (e.g., magnitude pruning)
    const int blocks_a = 4;
    const size_t size_a = (size_t)blocks_a * 4096;
    std::vector<uint8_t> data_a(size_a, 0);
    gen_random_sparse_data(data_a, size_a, 70);
    run_scenario("Scenario A: Random 70% Sparsity (unstructured pruning)",
                 data_a, blocks_a, 500000);

    // ---- Scenario B: Structured sparsity ----
    // Channel-pruned / block-sparse models (30% zero chunks, 10% dense chunks)
    const int blocks_b = 4;
    const size_t size_b = (size_t)blocks_b * 4096;
    std::vector<uint8_t> data_b(size_b, 0);
    gen_structured_sparse_data(data_b, size_b);
    run_scenario("Scenario B: Structured Sparsity (30% zero + 10% dense chunks)",
                 data_b, blocks_b, 500000);

    // ---- Analysis ----
    std::cout << "\n==================================================================" << std::endl;
    std::cout << "  Analysis: Why JIT Specialized Wins" << std::endl;
    std::cout << "==================================================================" << std::endl;

    std::cout << R"(
  The fundamental advantage of JIT is RUNTIME SPECIALIZATION:
  values that the compiler must treat as unknowns become compile-time
  constants in the JIT, enabling optimizations impossible for AOT code.

  ┌─────────────────────────────────────────────────────────────────┐
  │  Generic kernel (intrinsics / JIT-generic) per chunk:          │
  │                                                                │
  │    mov  rax, [bitmask + runtime_off]  ← memory load            │
  │    kmovq k1, rax                      ← k-mask setup           │
  │    vpexpandb zmm{k1}{z}, [src]        ← DATA-DEPENDENT addr    │
  │    popcnt rax, rax                    ← 3-cycle latency        │
  │    add  src, rax                      ← depends on popcnt      │
  │    vmovdqu8 [dst], zmm               ← store                  │
  │                                                                │
  │    Instructions: 6        Critical path: popcnt(3) + add(1)    │
  │    Dependency: src → expand → popcnt → add → next src          │
  └─────────────────────────────────────────────────────────────────┘

  ┌─────────────────────────────────────────────────────────────────┐
  │  JIT Specialized kernel per chunk:                             │
  │                                                                │
  │    mov  rax, 0x0101...    ← IMMEDIATE (no memory load)         │
  │    kmovq k1, rax          ← k-mask setup                      │
  │    vpexpandb zmm, [base + IMM_OFFSET]  ← STATIC address       │
  │    vmovdqu8 [dst + IMM], zmm           ← store                │
  │                                                                │
  │    Instructions: 4        Critical path: 0 address-computation │
  │    NO popcnt, NO pointer advance, NO dependency between chunks │
  └─────────────────────────────────────────────────────────────────┘

  Additional advantages for structured sparsity:
    - Zero-mask chunks → vpxord + store (2 insns, no memory read)
    - Dense chunks     → vmovdqu8 copy  (2 insns, no k-mask logic)

  When is this applicable?
    - LLM weight decompression (masks fixed at model load time)
    - Sparse matrix operations with fixed sparsity patterns
    - Any workload where data-dependent branching is constant across calls
)" << std::endl;

    return 0;
}
