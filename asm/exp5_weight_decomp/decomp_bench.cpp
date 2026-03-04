#include <iostream>
#include <vector>
#include <cstdint>
#include <cstring>  // memset, memcmp
#include <cassert>
#include <chrono>
#include <cstdlib>  // aligned_alloc, free
#include <iomanip>  // 用于 std::setw, std::fixed, std::hex, std::setfill

#include "decomp_kernel_ref.hpp"
#include "decomp_kernel_jit.hpp"
#include "decomp_kernel_avx512.hpp"

void gen_origin_data(std::vector<uint8_t> original_uncompressed_data,
                     const size_t output_size) {
    std::cout << "Step 1: Original uncompressed sparse data created (Random)." << std::endl;
    // 使用随机数据填充，模拟稀疏性
    srand(42);
    for (size_t i = 0; i < output_size; ++i) {
        // 70% 概率为 0
        if (rand() % 10 < 7) {
            original_uncompressed_data[i] = 0;
        } else {
            original_uncompressed_data[i] = (uint8_t)((rand() % 255) + 1);
        }
    }
}

// =================================================================
// ---- 新增的压缩函数 ----
// 这个函数接收未压缩的稀疏数据，并在压缩过程中生成掩码和压缩后的数据流
// =================================================================
void compress_weights(
    std::vector<uint8_t>& compressed_output,
    std::vector<uint64_t>& bitmask_output,
    const std::vector<uint8_t>& uncompressed_input,
    int num_blocks,
    int chunks_per_block) {
    
    compressed_output.clear(); // data size is aligned with 64B instead of address.
    bitmask_output.resize(num_blocks * chunks_per_block);

    for (int block = 0; block < num_blocks; ++block) {
        for (int cl = 0; cl < chunks_per_block; ++cl) {
            size_t chunk_idx = (size_t)block * chunks_per_block + cl;
            const uint8_t* uncompressed_chunk_ptr = &uncompressed_input[chunk_idx * 64];

            // 遍历原始数据，生成掩码并提取非零字节
            uint64_t mask = 0;
            for (int b = 0; b < 64; ++b) {
                if (uncompressed_chunk_ptr[b] != 0) {
                    mask |= (1ULL << b);
                    compressed_output.push_back(uncompressed_chunk_ptr[b]);
                }
            }
            bitmask_output[chunk_idx] = mask;
        }

        // --- 缓存行对齐逻辑 (与内核中镜像) ---
        // 在compressed data每个block的末尾，添加填充以模拟指针推进
        size_t current_size = compressed_output.size();
        size_t misalignment = current_size & 0x3F;
        if (misalignment != 0) {
            size_t padding_needed = 64 - misalignment;
            for (size_t p = 0; p < padding_needed; ++p) {
                compressed_output.push_back(0xDD); // 插入“死”字节作为填充
            }
        }
    }
    
    // 确保末尾有足够的填充，以防越界读取
    compressed_output.resize(compressed_output.size() + 128, 0xDD);
    std::cout << "Step 2: Compression logic executed." << std::endl;
}

// -----------------------------------------------------------------
// ---- 5. Main 函数 - 基准测试和验证 ----
// -----------------------------------------------------------------
int main() {
    // --- 1. 设置测试参数 ---
    const int num_blocks = 1000; // 运行 1000 个 4KB 的块
    const int elts_per_block = 4096;
    const int chunks_per_block = 64;
    const int total_chunks = num_blocks * chunks_per_block;
    const size_t output_size = (size_t)num_blocks * elts_per_block;

    std::cout << "--- AVX-512 Decompression Benchmark (Refactored) ---" << std::endl;
    std::cout << "Total output size: " << (output_size / 1024.0 / 1024.0) << " MB" << std::endl;

    // --- 2. 分配缓冲区 ---

    // 64 字节对齐输出缓冲区
    uint8_t* output_scalar_nounroll = (uint8_t*)aligned_alloc(64, output_size);
    uint8_t* output_scalar_unroll = (uint8_t*)aligned_alloc(64, output_size);
    uint8_t* output_avx512_nounroll = (uint8_t*)aligned_alloc(64, output_size);
    uint8_t* output_avx512_unroll = (uint8_t*)aligned_alloc(64, output_size);
    uint8_t* output_jit_avx512_unroll = (uint8_t*)aligned_alloc(64, output_size);

    if (!output_scalar_nounroll || !output_scalar_unroll || !output_avx512_nounroll || !output_avx512_unroll) {
        std::cerr << "Failed to allocate aligned memory" << std::endl;
        return 1;
    }

    // =================================================================
    // ---- 第 1 步: 构建原始非压缩数据 ----
    // =================================================================
    std::vector<uint8_t> original_uncompressed_data(output_size, 0);
    gen_origin_data(original_uncompressed_data, output_size);

    // =================================================================
    // ---- 第 2 步: 实现压缩逻辑 (同时生成掩码) ----
    // =================================================================
    std::vector<uint8_t> compressed_data;
    std::vector<uint64_t> bitmask(total_chunks);
    compress_weights(compressed_data, bitmask, original_uncompressed_data, num_blocks, chunks_per_block);
    std::cout << "Compressed size (with padding): " << (compressed_data.size() / 1024.0 / 1024.0) << " MB (70% sparsity)" << std::endl;
    
    // =================================================================
    // ---- 第 3 步: 运行基准测试和验证 ----
    // (验证逻辑现在与 original_uncompressed_data 进行比较)
    // =================================================================
    std::cout << "\nRunning benchmarks..." << std::endl;

    // --- 标量 (不展开) ---
    memset(output_scalar_nounroll, 0, output_size);
    auto start_s_nounroll = std::chrono::high_resolution_clock::now();
    decompress_scalar_nounroll(output_scalar_nounroll, compressed_data.data(), bitmask.data(), num_blocks);
    auto end_s_nounroll = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double, std::milli> time_s_nounroll = end_s_nounroll - start_s_nounroll;
    if (memcmp(output_scalar_nounroll, original_uncompressed_data.data(), output_size) != 0) {
        std::cerr << "FAILURE: Scalar (No Unroll) verification failed!" << std::endl;
    } else {
        std::cout << "SUCCESS: Scalar (No Unroll) verified." << std::endl;
    }

    // --- 标量 (4x 展开) ---
    memset(output_scalar_unroll, 0, output_size);
    auto start_s_unroll = std::chrono::high_resolution_clock::now();
    decompress_scalar(output_scalar_unroll, compressed_data.data(), bitmask.data(), num_blocks);
    auto end_s_unroll = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double, std::milli> time_s_unroll = end_s_unroll - start_s_unroll;
    if (memcmp(output_scalar_unroll, original_uncompressed_data.data(), output_size) != 0) {
        std::cerr << "FAILURE: Scalar (Unrolled) verification failed!" << std::endl;
    } else {
        std::cout << "SUCCESS: Scalar (Unrolled) verified." << std::endl;
    }

    // --- AVX-512 (不展开) ---
    memset(output_avx512_nounroll, 0, output_size);
    auto start_avx_nounroll = std::chrono::high_resolution_clock::now();
    decompress_avx512_nounroll(output_avx512_nounroll, compressed_data.data(), bitmask.data(), num_blocks);
    auto end_avx_nounroll = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double, std::milli> time_avx_nounroll = end_avx_nounroll - start_avx_nounroll;
    if (memcmp(output_avx512_nounroll, original_uncompressed_data.data(), output_size) != 0) {
        std::cerr << "FAILURE: AVX-512 (No Unroll) verification failed!" << std::endl;
    } else {
        std::cout << "SUCCESS: AVX-512 (No Unroll) verified." << std::endl;
    }

    // --- AVX-512 (4x 展开) ---
    memset(output_avx512_unroll, 0, output_size);
    auto start_avx_unroll = std::chrono::high_resolution_clock::now();
    decompress_avx512(output_avx512_unroll, compressed_data.data(), bitmask.data(), num_blocks);
    auto end_avx_unroll = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double, std::milli> time_avx_unroll = end_avx_unroll - start_avx_unroll;
    if (memcmp(output_avx512_unroll, original_uncompressed_data.data(), output_size) != 0) {
        std::cerr << "FAILURE: AVX-512 (Unrolled) verification failed!" << std::endl;
    } else {
        std::cout << "SUCCESS: AVX-512 (Unrolled) verified." << std::endl;
    }

    // --- JIT AVX-512 (4x 展开) ---
    memset(output_avx512_unroll, 0, output_size);
    jit_decompress_kernel_t kernel(num_blocks);
    auto* generated_jit_avx512_unroll_func = kernel.getCode<void (*)(jit_decomp_params_t*)>();
    kernel.ready();

    jit_decomp_params_t args;
    args.compressed_buf = compressed_data.data();
    args.bitmask_ptr = bitmask.data();
    args.decomp_buf = (void*)output_jit_avx512_unroll;

    auto start_jit_unroll = std::chrono::high_resolution_clock::now();
    generated_jit_avx512_unroll_func(&args);
    auto end_jit_unroll = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double, std::milli> time_jit_unroll = end_jit_unroll - start_jit_unroll;
    if (memcmp(output_avx512_unroll, original_uncompressed_data.data(), output_size) != 0) {
        std::cerr << "FAILURE: AVX-512 (Unrolled) verification failed!" << std::endl;
    } else {
        std::cout << "SUCCESS: AVX-512 (Unrolled) verified." << std::endl;
    }

    // --- 4. 打印结果 ---
    std::cout << "\n--- Performance Results ---" << std::endl;
    std::cout << std::fixed << std::setprecision(3);
    const int w = 30;
    
    std::cout << std::setw(w) << std::left << "Scalar (No Unroll)" << ": " 
              << std::setw(10) << std::right << time_s_nounroll.count() << " ms\t"
              << "(Baseline, 1.00x)" << std::endl;
              
    std::cout << std::setw(w) << std::left << "Scalar (4x Unroll)" << ": " 
              << std::setw(10) << std::right << time_s_unroll.count() << " ms\t"
              << "(" << (time_s_nounroll.count() / time_s_unroll.count()) << "x vs Scalar-NoUnroll)" << std::endl;

    std::cout << std::setw(w) << std::left << "AVX-512 (No Unroll)" << ": " 
              << std::setw(10) << std::right << time_avx_nounroll.count() << " ms\t"
              << "(" << (time_s_nounroll.count() / time_avx_nounroll.count()) << "x vs Scalar-NoUnroll)" << std::endl;
              
    std::cout << std::setw(w) << std::left << "AVX-512 (4x Unroll)" << ": " 
              << std::setw(10) << std::right << time_avx_unroll.count() << " ms\t"
              << "(" << (time_s_nounroll.count() / time_avx_unroll.count()) << "x vs Scalar-NoUnroll)" << std::endl;

    std::cout << std::setw(w) << std::left << "JIT-AVX-512 (4x Unroll, JIT)" << ": " 
              << std::setw(10) << std::right << time_jit_unroll.count() << " ms\t"
              << "(" << (time_s_nounroll.count() / time_jit_unroll.count()) << "x vs Scalar-NoUnroll)" << std::endl;

    std::cout << "\n--- Key Speedups ---" << std::endl;
    std::cout << "SIMD (AVX-512) Speedup: " << (time_s_nounroll.count() / time_avx_nounroll.count()) << "x" << std::endl;
    std::cout << "Unrolling Speedup (AVX-512): " << (time_avx_nounroll.count() / time_avx_unroll.count()) << "x" << std::endl;

    // --- 5. 打印前 10 个值 ---
    std::cout << "\n--- Value Comparison (First 10 Bytes) ---" << std::endl;
    std::cout << std::hex << std::setfill('0'); // 切换到十六进制打印
    
    std::cout << std::setw(w) << std::left << "Original Data" << ": ";
    for (int i = 0; i < 10; ++i) {
        std::cout << "0x" << std::setw(2) << (int)original_uncompressed_data[i] << " ";
    }
    std::cout << std::endl;
    
    std::cout << std::setw(w) << std::left << "Decompressed (AVX-512 Unrolled)" << ": ";
    for (int i = 0; i < 10; ++i) {
        std::cout << "0x" << std::setw(2) << (int)output_avx512_unroll[i] << " ";
    }
    std::cout << std::endl << std::dec; // 切换回十进制

    // 释放内存
    free(output_scalar_nounroll);
    free(output_scalar_unroll);
    free(output_avx512_nounroll);
    free(output_avx512_unroll);
    free(output_jit_avx512_unroll);

    return 0;
}
