#include <iostream>
#include <vector>
#include <cstdint>
#include <cstring>  // memset, memcmp
#include <cassert>
#include <chrono>
#include <cstdlib>  // aligned_alloc, free
#include <iomanip>  // 用于 std::setw, std::fixed, std::hex, std::setfill

// AVX-512 和 POPCNT 的头文件
#include <immintrin.h>
#include <nmmintrin.h> // for _mm_popcnt_u64

// -----------------------------------------------------------------
// ---- 1. 纯 C++ (标量) 实现 - 不展开 ----
// (此函数无变化)
// -----------------------------------------------------------------
void decompress_scalar_nounroll(uint8_t* scratch_buf,
                                const uint8_t* ptr_B,
                                const uint64_t* bitmask_ptr,
                                int blocks) {
    
    const uint8_t* current_src_ptr = ptr_B;
    const int chunks_per_block = 64; // 4096 / 64 = 64

    for (int block = 0; block < blocks; ++block) {
        size_t wei_offset = (size_t)block * 4096;
        const uint64_t* current_mask_ptr = bitmask_ptr + (block * chunks_per_block);

        // 循环步进改为 1 (cl++)
        for (int cl = 0; cl < chunks_per_block; cl++) {
            
            // --- 只保留 1 次循环体 ---
            uint64_t mask1 = current_mask_ptr[cl];
            uint8_t* dst1 = scratch_buf + wei_offset + cl * 64;
            size_t popcnt1 = 0;
            for (int i = 0; i < 64; ++i) {
                if ((mask1 >> i) & 1) {
                    dst1[i] = current_src_ptr[popcnt1++];
                } else {
                    dst1[i] = 0;
                }
            }
            current_src_ptr += popcnt1; // 推进指针
        }

        // --- 缓存行对齐逻辑 (不变) ---
        size_t offset = (size_t)current_src_ptr;
        size_t misalignment = offset & 0x3F; // 6 LSBs (63)
        if (misalignment != 0) {
            current_src_ptr += (64 - misalignment);
        }
    }
}

// -----------------------------------------------------------------
// ---- 2. 纯 C++ (标量) 实现 - 4x 展开 ----
// (此函数无变化)
// -----------------------------------------------------------------
void decompress_scalar(uint8_t* scratch_buf,
                       const uint8_t* ptr_B,
                       const uint64_t* bitmask_ptr,
                       int blocks) {
    
    const uint8_t* current_src_ptr = ptr_B;
    const int chunks_per_block = 64; // 4096 / 64 = 64

    for (int block = 0; block < blocks; ++block) {
        size_t wei_offset = (size_t)block * 4096;
        const uint64_t* current_mask_ptr = bitmask_ptr + (block * chunks_per_block);

        // 4x 循环展开 (与 JIT 代码匹配)
        for (int cl = 0; cl < chunks_per_block; cl += 4) {
            
            // --- Unroll 1 ---
            uint64_t mask1 = current_mask_ptr[cl + 0];
            uint8_t* dst1 = scratch_buf + wei_offset + (cl + 0) * 64;
            size_t popcnt1 = 0;
            for (int i = 0; i < 64; ++i) {
                if ((mask1 >> i) & 1) {
                    dst1[i] = current_src_ptr[popcnt1++];
                } else {
                    dst1[i] = 0;
                }
            }
            current_src_ptr += popcnt1;

            // --- Unroll 2 ---
            uint64_t mask2 = current_mask_ptr[cl + 1];
            uint8_t* dst2 = scratch_buf + wei_offset + (cl + 1) * 64;
            size_t popcnt2 = 0;
            for (int i = 0; i < 64; ++i) {
                if ((mask2 >> i) & 1) {
                    dst2[i] = current_src_ptr[popcnt2++];
                } else {
                    dst2[i] = 0;
                }
            }
            current_src_ptr += popcnt2;
            
            // --- Unroll 3 ---
            uint64_t mask3 = current_mask_ptr[cl + 2];
            uint8_t* dst3 = scratch_buf + wei_offset + (cl + 2) * 64;
            size_t popcnt3 = 0;
            for (int i = 0; i < 64; ++i) {
                if ((mask3 >> i) & 1) {
                    dst3[i] = current_src_ptr[popcnt3++];
                } else {
                    dst3[i] = 0;
                }
            }
            current_src_ptr += popcnt3;

            // --- Unroll 4 ---
            uint64_t mask4 = current_mask_ptr[cl + 3];
            uint8_t* dst4 = scratch_buf + wei_offset + (cl + 3) * 64;
            size_t popcnt4 = 0;
            for (int i = 0; i < 64; ++i) {
                if ((mask4 >> i) & 1) {
                    dst4[i] = current_src_ptr[popcnt4++];
                } else {
                    dst4[i] = 0;
                }
            }
            current_src_ptr += popcnt4;
        }

        // --- 缓存行对齐逻辑 (不变) ---
        size_t offset = (size_t)current_src_ptr;
        size_t misalignment = offset & 0x3F;
        if (misalignment != 0) {
            current_src_ptr += (64 - misalignment);
        }
    }
}


// -----------------------------------------------------------------
// ---- 3. AVX-512 Intrinsics 实现 - 不展开 ----
// (此函数无变化)
// -----------------------------------------------------------------
void decompress_avx512_nounroll(uint8_t* scratch_buf,
                                const uint8_t* ptr_B,
                                const uint64_t* bitmask_ptr,
                                int blocks) {
    
    const uint8_t* current_src_ptr = ptr_B;
    const int chunks_per_block = 64;

    for (int block = 0; block < blocks; ++block) {
        size_t wei_offset = (size_t)block * 4096;
        const uint64_t* current_mask_ptr = bitmask_ptr + (block * chunks_per_block);

        // 循环步进改为 1 (cl++)
        for (int cl = 0; cl < chunks_per_block; cl++) {
            
            // --- 只保留 1 次循环体 ---
            uint64_t mask1_u64 = current_mask_ptr[cl];
            __mmask64 mask1 = mask1_u64;
            
            __m512i zmm1 = _mm512_maskz_expandloadu_epi8(mask1, current_src_ptr);
            
            _mm512_storeu_si512((__m512i*)(scratch_buf + wei_offset + cl * 64), zmm1);
            
            current_src_ptr += _mm_popcnt_u64(mask1_u64);
        }

        // --- 缓存行对齐逻辑 (不变) ---
        size_t offset = (size_t)current_src_ptr;
        size_t misalignment = offset & 0x3F;
        if (misalignment != 0) {
            current_src_ptr += (64 - misalignment);
        }
    }
}


// -----------------------------------------------------------------
// ---- 4. AVX-512 Intrinsics 实现 - 4x 展开 ----
// (此函数无变化)
// -----------------------------------------------------------------
void decompress_avx512(uint8_t* scratch_buf,
                       const uint8_t* ptr_B,
                       const uint64_t* bitmask_ptr,
                       int blocks) {
    
    const uint8_t* current_src_ptr = ptr_B;
    const int chunks_per_block = 64;

    for (int block = 0; block < blocks; ++block) {
        size_t wei_offset = (size_t)block * 4096;
        const uint64_t* current_mask_ptr = bitmask_ptr + (block * chunks_per_block);

        for (int cl = 0; cl < chunks_per_block; cl += 4) {
            // --- Unroll 1 ---
            uint64_t mask1_u64 = current_mask_ptr[cl + 0];
            __mmask64 mask1 = mask1_u64;
            __m512i zmm1 = _mm512_maskz_expandloadu_epi8(mask1, current_src_ptr);
            _mm512_storeu_si512((__m512i*)(scratch_buf + wei_offset + (cl + 0) * 64), zmm1);
            current_src_ptr += _mm_popcnt_u64(mask1_u64);

            // --- Unroll 2 ---
            uint64_t mask2_u64 = current_mask_ptr[cl + 1];
            __mmask64 mask2 = mask2_u64;
            __m512i zmm2 = _mm512_maskz_expandloadu_epi8(mask2, current_src_ptr);
            _mm512_storeu_si512((__m512i*)(scratch_buf + wei_offset + (cl + 1) * 64), zmm2);
            current_src_ptr += _mm_popcnt_u64(mask2_u64);

            // --- Unroll 3 ---
            uint64_t mask3_u64 = current_mask_ptr[cl + 2];
            __mmask64 mask3 = mask3_u64;
            __m512i zmm3 = _mm512_maskz_expandloadu_epi8(mask3, current_src_ptr);
            _mm512_storeu_si512((__m512i*)(scratch_buf + wei_offset + (cl + 2) * 64), zmm3);
            current_src_ptr += _mm_popcnt_u64(mask3_u64);

            // --- Unroll 4 ---
            uint64_t mask4_u64 = current_mask_ptr[cl + 3];
            __mmask64 mask4 = mask4_u64;
            __m512i zmm4 = _mm512_maskz_expandloadu_epi8(mask4, current_src_ptr);
            _mm512_storeu_si512((__m512i*)(scratch_buf + wei_offset + (cl + 3) * 64), zmm4);
            current_src_ptr += _mm_popcnt_u64(mask4_u64);
        }

        // --- 缓存行对齐逻辑 (不变) ---
        size_t offset = (size_t)current_src_ptr;
        size_t misalignment = offset & 0x3F;
        if (misalignment != 0) {
            current_src_ptr += (64 - misalignment);
        }
    }
}


// -----------------------------------------------------------------
// ---- 5. Main 函数 - 基准测试和验证 ----
// -----------------------------------------------------------------

int main() {
    // --- 1. 设置测试参数 ---
    const int num_blocks = 1000; // 运行 1000 个 4KB 的块
    const int elts_per_block = 4096;
    const int chunks_per_block = 64;
    const int elts_per_chunk = 64;

    const int total_chunks = num_blocks * chunks_per_block;
    const size_t output_size = (size_t)num_blocks * elts_per_block;

    std::cout << "--- AVX-512 Decompression Benchmark (All Versions) ---" << std::endl;
    std::cout << "Total output size: " << (output_size / 1024.0 / 1024.0) << " MB" << std::endl;

    // --- 2. 分配和创建测试数据 (已修正 Bug) ---
    std::vector<uint64_t> bitmask(total_chunks);
    std::vector<uint8_t> compressed_data;
    std::vector<uint8_t> expected_output(output_size, 0);

    // 64 字节对齐输出缓冲区
    uint8_t* output_scalar_nounroll = (uint8_t*)aligned_alloc(64, output_size);
    uint8_t* output_scalar_unroll = (uint8_t*)aligned_alloc(64, output_size);
    uint8_t* output_avx512_nounroll = (uint8_t*)aligned_alloc(64, output_size);
    uint8_t* output_avx512_unroll = (uint8_t*)aligned_alloc(64, output_size);

    if (!output_scalar_nounroll || !output_scalar_unroll || !output_avx512_nounroll || !output_avx512_unroll) {
        std::cerr << "Failed to allocate aligned memory" << std::endl;
        return 1;
    }

    // *** 修正后的数据生成逻辑 ***
    size_t current_src_size = 0; // 跟踪 compressed_data 的逻辑大小

    for (int block = 0; block < num_blocks; ++block) {
        for (int cl = 0; cl < chunks_per_block; ++cl) {
            
            size_t i = (size_t)block * chunks_per_block + cl; // 全局 chunk 索引

            // 创建一个 50% 稀疏度的掩码 (0b10101010...)
            uint64_t mask = 0xAAAAAAAAAAAAAAAA;
            bitmask[i] = mask;
            size_t popcnt = _mm_popcnt_u64(mask); // popcnt = 32

            // 1. 准备要插入的数据
            std::vector<uint8_t> values_to_insert;
            for (size_t p = 0; p < popcnt; ++p) {
                // 使用 (i % 256) 来生成可预测但会回绕的字节值
                values_to_insert.push_back((uint8_t)(i + p));
            }

            // 2. 填充预期输出 (此逻辑不变, 始终正确)
            size_t out_offset = i * 64;
            size_t data_idx = 0;
            for (int b = 0; b < 64; ++b) {
                if ((mask >> b) & 1) {
                    expected_output[out_offset + b] = values_to_insert[data_idx];
                    data_idx++;
                }
            }
            
            // 3. 填充压缩数据
            compressed_data.insert(compressed_data.end(), values_to_insert.begin(), values_to_insert.end());
            current_src_size += popcnt; // 更新逻辑大小
        }

        // --- 缓存行对齐逻辑 (镜像内核) ---
        // 在每个块的末尾，向 compressed_data 添加填充以模拟指针推进
        size_t misalignment = current_src_size & 0x3F;
        if (misalignment != 0) {
            size_t padding_needed = 64 - misalignment;
            for (size_t p = 0; p < padding_needed; ++p) {
                compressed_data.push_back(0xDD); // 插入“死”字节作为填充
            }
            current_src_size += padding_needed; // 更新跟踪的逻辑大小
        }
    }
    
    // 确保末尾有足够的填充
    compressed_data.resize(current_src_size + 128, 0xDD);
    size_t final_src_size = current_src_size; // 最终包含填充的大小

    std::cout << "Test data generated. Compressed size (with padding): " 
              << (final_src_size / 1024.0 / 1024.0) << " MB (50% sparsity)" << std::endl;
    
    // --- 3. 运行基准测试 ---
    std::cout << "\nRunning benchmarks..." << std::endl;

    // --- 标量 (不展开) ---
    memset(output_scalar_nounroll, 0, output_size);
    auto start_s_nounroll = std::chrono::high_resolution_clock::now();
    decompress_scalar_nounroll(output_scalar_nounroll, compressed_data.data(), bitmask.data(), num_blocks);
    auto end_s_nounroll = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double, std::milli> time_s_nounroll = end_s_nounroll - start_s_nounroll;
    if (memcmp(output_scalar_nounroll, expected_output.data(), output_size) != 0) {
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
    if (memcmp(output_scalar_unroll, expected_output.data(), output_size) != 0) {
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
    if (memcmp(output_avx512_nounroll, expected_output.data(), output_size) != 0) {
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
    if (memcmp(output_avx512_unroll, expected_output.data(), output_size) != 0) {
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
              
    std::cout << std::setw(w) << std::left << "AVX-512 (4x Unroll, JIT)" << ": " 
              << std::setw(10) << std::right << time_avx_unroll.count() << " ms\t"
              << "(" << (time_s_nounroll.count() / time_avx_unroll.count()) << "x vs Scalar-NoUnroll)" << std::endl;

    std::cout << "\n--- Key Speedups ---" << std::endl;
    std::cout << "SIMD (AVX-512) Speedup: " << (time_s_nounroll.count() / time_avx_nounroll.count()) << "x" << std::endl;
    std::cout << "Unrolling Speedup (AVX-512): " << (time_avx_nounroll.count() / time_avx_unroll.count()) << "x" << std::endl;

    // --- 5. 打印前 10 个值 ---
    std::cout << "\n--- Value Comparison (First 10 Bytes) ---" << std::endl;
    std::cout << std::hex << std::setfill('0'); // 切换到十六进制打印
    
    std::cout << std::setw(w) << std::left << "Expected Output" << ": ";
    for (int i = 0; i < 10; ++i) {
        std::cout << "0x" << std::setw(2) << (int)expected_output[i] << " ";
    }
    std::cout << std::endl;
    
    std::cout << std::setw(w) << std::left << "Actual (AVX-512 Unrolled)" << ": ";
    for (int i = 0; i < 10; ++i) {
        std::cout << "0x" << std::setw(2) << (int)output_avx512_unroll[i] << " ";
    }
    std::cout << std::endl << std::dec; // 切换回十进制

    // 释放内存
    free(output_scalar_nounroll);
    free(output_scalar_unroll);
    free(output_avx512_nounroll);
    free(output_avx512_unroll);

    return 0;
}