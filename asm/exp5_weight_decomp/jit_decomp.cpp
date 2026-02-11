#include <iostream>
#include <vector>
#include <cstdint>
#include <cstddef>  // 用于 offsetof
#include <cstring>  // 用于 memset
#include <cassert>
#include <sys/mman.h> // mmap, mprotect, munmap

// 包含 xbyak 头文件
#include "xbyak/xbyak.h"

// -----------------------------------------------------------------
// 1. 定义 JIT 内核将访问的参数结构体
// -----------------------------------------------------------------

struct jit_brgemm_params_t {
    const void* ptr_B;        // 指向压缩权重
    const void* bitmask_ptr;  // 指向位掩码
    void* scratch_buf;  // 指向解压的目标缓冲区
};

// 定义宏以匹配原始代码中的 GET_OFF
#define GET_OFF(field) (offsetof(jit_brgemm_params_t, field))

// -----------------------------------------------------------------
// 2. 完整的 JIT 内核类 (包含您的 generate() 函数)
// -----------------------------------------------------------------

class jit_decompress_kernel_t : public Xbyak::CodeGenerator {
public:
    // --- 寄存器定义 ---
    // 假设是 Linux x64 ABI: param1 在 rdi
    Xbyak::Reg64 param1 = rdi;

    // 工作指针
    Xbyak::Reg64 wei_ptr = r8;
    Xbyak::Reg64 reg_ptr_decomp_mask = r9;
    Xbyak::Reg64 reg_ptr_decomp_dst = r10;
    Xbyak::Reg64 reg_ptr_decomp_src = r11;

    // 临时通用寄存器
    Xbyak::Reg64 reg_comp_mask_tmp1 = r12;
    Xbyak::Reg64 reg_comp_mask_tmp2 = r13;
    Xbyak::Reg64 reg_comp_mask_tmp3 = r14;
    Xbyak::Reg64 reg_comp_mask_tmp4 = r15;
    Xbyak::Reg64 reg_popcnt = rax;
    Xbyak::Reg64 reg_ptr_decomp_src_align = rdx;

    // K-Mask (Opmask) 寄存器
    Xbyak::Opmask reg_comp_mask1 = k1;
    Xbyak::Opmask reg_comp_mask2 = k2;
    Xbyak::Opmask reg_comp_mask3 = k3;
    Xbyak::Opmask reg_comp_mask4 = k4;

    // ZMM (AVX-512) 寄存器
    Xbyak::Zmm zmm_comp1 = zmm0;
    Xbyak::Zmm zmm_comp2 = zmm1;
    Xbyak::Zmm zmm_comp3 = zmm2;
    Xbyak::Zmm zmm_comp4 = zmm3;

    // xbyak 定义的零掩码 (Zeroing mask)
    // Xbyak::Emask T_z = T_z;

    // 成员变量
    int blocks_;

    // --- 构造函数 ---
    jit_decompress_kernel_t(int blocks) : Xbyak::CodeGenerator(4096 * 16), blocks_(blocks) {
        generate(); // JIT 编译在构造时发生
    }

    // --- 您的 generate() 函数 ---
    // (已从您的提示中复制并粘贴到此类中)
    void generate() {
        // preamble();
        // --- 替换 preamble(); ---
        // 保存 GPRs (r12-r15)
        push(r12);
        push(r13);
        push(r14);
        push(r15);

        // 保存 K 寄存器 (k1-k4)
        // 无法直接 push K 寄存器, 必须先 mov 到 GPR
        kmovq(rax, k1); push(rax);
        kmovq(rax, k2); push(rax);
        kmovq(rax, k3); push(rax);
        kmovq(rax, k4); push(rax);

        mov(wei_ptr, ptr[param1 + GET_OFF(ptr_B)]);
        mov(reg_ptr_decomp_mask, ptr[param1 + GET_OFF(bitmask_ptr)]);
        mov(reg_ptr_decomp_dst, ptr[param1 + GET_OFF(scratch_buf)]);
        lea(reg_ptr_decomp_src, ptr[wei_ptr]);

        for (int block = 0; block < blocks_; block++) {
            int wei_offset = block * 4096;
            int bitmask_off = wei_offset / (1 * 8);

            for (int cl = 0; cl < 64; cl = cl + 4) {
                mov(reg_comp_mask_tmp1,
                        ptr[reg_ptr_decomp_mask + cl * 8 + bitmask_off]);
                kmovq(reg_comp_mask1, reg_comp_mask_tmp1);

                mov(reg_comp_mask_tmp2,
                        ptr[reg_ptr_decomp_mask + (cl + 1) * 8 + bitmask_off]);
                kmovq(reg_comp_mask2, reg_comp_mask_tmp2);

                mov(reg_comp_mask_tmp3,
                        ptr[reg_ptr_decomp_mask + (cl + 2) * 8 + bitmask_off]);
                kmovq(reg_comp_mask3, reg_comp_mask_tmp3);

                mov(reg_comp_mask_tmp4,
                        ptr[reg_ptr_decomp_mask + (cl + 3) * 8 + bitmask_off]);
                kmovq(reg_comp_mask4, reg_comp_mask_tmp4);

                vmovdqu8(zmm_comp1, ptr[reg_ptr_decomp_src]);
                popcnt(reg_popcnt, reg_comp_mask_tmp1);
                add(reg_ptr_decomp_src, reg_popcnt);

                vmovdqu8(zmm_comp2, ptr[reg_ptr_decomp_src]);
                popcnt(reg_popcnt, reg_comp_mask_tmp2);
                add(reg_ptr_decomp_src, reg_popcnt);

                vmovdqu8(zmm_comp3, ptr[reg_ptr_decomp_src]);
                popcnt(reg_popcnt, reg_comp_mask_tmp3);
                add(reg_ptr_decomp_src, reg_popcnt);

                vmovdqu8(zmm_comp4, ptr[reg_ptr_decomp_src]);
                popcnt(reg_popcnt, reg_comp_mask_tmp4);
                add(reg_ptr_decomp_src, reg_popcnt);

                vpexpandb(zmm_comp1 | reg_comp_mask1 | T_z, zmm_comp1);
                vmovdqu8(ptr[reg_ptr_decomp_dst + wei_offset + cl * 64], zmm_comp1);

                vpexpandb(zmm_comp2 | reg_comp_mask2 | T_z, zmm_comp2);
                vmovdqu8(ptr[reg_ptr_decomp_dst + wei_offset + (cl + 1) * 64],
                        zmm_comp2);

                vpexpandb(zmm_comp3 | reg_comp_mask3 | T_z, zmm_comp3);
                vmovdqu8(ptr[reg_ptr_decomp_dst + wei_offset + (cl + 2) * 64],
                        zmm_comp3);

                vpexpandb(zmm_comp4 | reg_comp_mask4 | T_z, zmm_comp4);
                vmovdqu8(ptr[reg_ptr_decomp_dst + wei_offset + (cl + 3) * 64],
                        zmm_comp4);
            }

            // XXX: memory alignment of weights buffer can lead to issues.
            mov(reg_ptr_decomp_src_align, reg_ptr_decomp_src);
            not_(reg_ptr_decomp_src_align);
            and_(reg_ptr_decomp_src_align, 0x3f); // get 6 LSBs
            add(reg_ptr_decomp_src_align, 0x1);
            and_(reg_ptr_decomp_src_align,
                    0x3f); // 0x0 if already aligned to cacheline
            add(reg_ptr_decomp_src, reg_ptr_decomp_src_align);
        }
        // postamble();
        // --- 替换 postamble(); ---
        // 恢复 K 寄存器 (k1-k4) (顺序相反)
        pop(rax); kmovq(k4, rax);
        pop(rax); kmovq(k3, rax);
        pop(rax); kmovq(k2, rax);
        pop(rax); kmovq(k1, rax);

        // 恢复 GPRs (r12-r15) (顺序相反)
        pop(r15);
        pop(r14);
        pop(r13);
        pop(r12);

        ret(); // 函数返回
    }
};

// -----------------------------------------------------------------
// 3. Main() 函数 - 测试样本
// -----------------------------------------------------------------

// 辅助函数：打印 ZMM 大小的缓冲区
void print_buffer(const char* title, const uint8_t* buf) {
    printf("%s:\n", title);
    for (int i = 0; i < 64; ++i) {
        printf("%02X ", buf[i]);
        if ((i + 1) % 16 == 0) printf("\n");
    }
    printf("\n");
}

int main() {
    std::cout << "Starting JIT Decompression Test (requires AVX-512)..." << std::endl;

    // --- 1. 定义测试参数 ---
    const int num_blocks = 1;
    const int chunks_per_block = 64; // cl 循环从 0 到 60 (共 64 个)
    const int chunk_size_bytes = 64; // ZMM 寄存器大小
    const int output_size = num_blocks * chunks_per_block * chunk_size_bytes; // 1*64*64 = 4096 字节

    // --- 2. 分配和创建测试数据 ---

    // 掩码：每个 chunk (64字节) 需要一个 64-bit 掩码
    std::vector<uint64_t> bitmask(chunks_per_block);

    // 压缩数据：大小是动态的，取决于 popcnt
    std::vector<uint8_t> compressed_data;

    // 预期输出：用于验证
    std::vector<uint8_t> expected_output(output_size, 0);

    // 目标缓冲区（实际输出）
    // 使用 aligned_alloc 以确保缓冲区对齐
    uint8_t* actual_output = (uint8_t*)aligned_alloc(64, output_size);
    memset(actual_output, 0, output_size);

    uint8_t data_val_counter = 0;

    for (int i = 0; i < chunks_per_block; ++i) {
        // 创建一个简单的、可预测的掩码
        // 例如：只选择第 0, 8, 16, 24, 32, 40, 48, 56 字节
        uint64_t mask = 0x0101010101010101;
        bitmask[i] = mask;

        // 计算这个掩码中有多少 '1'
        int popcnt = __builtin_popcountll(mask); // 8

        // 1. 填充压缩数据
        // 我们需要添加 'popcnt' 个字节的数据
        for (int p = 0; p < popcnt; ++p) {
            uint8_t val = static_cast<uint8_t>(0xA0 + i + p);
            compressed_data.push_back(val);
        }

        // 2. 填充预期输出
        size_t output_offset = i * chunk_size_bytes;
        int data_idx = 0;
        for (int bit = 0; bit < 64; ++bit) {
            if ((mask >> bit) & 1) {
                // 这是 'vpexpandb' 将放置数据的地方
                uint8_t val = static_cast<uint8_t>(0xA0 + i + data_idx);
                expected_output[output_offset + bit] = val;
                data_idx++;
            }
        }
    }

    // 确保压缩数据有足够的填充，以防对齐逻辑读取超出范围
    compressed_data.resize(compressed_data.size() + 128);


    std::cout << "Test data created." << std::endl;
    std::cout << " - Bitmask size: " << bitmask.size() * 8 << " bytes" << std::endl;
    std::cout << " - Compressed data size: " << compressed_data.size() - 128 << " bytes" << std::endl;
    std::cout << " - Output buffer size: " << output_size << " bytes" << std::endl;

    // --- 3. JIT 编译内核 ---
    jit_decompress_kernel_t kernel(num_blocks);

    // 获取指向 JIT 生成的机器码的函数指针
    auto* generated_func = kernel.getCode<void (*)(jit_brgemm_params_t*)>();

    // 使代码可执行 (xbyak 默认是可写的)
    // mprotect 是更安全的方式，但 kernel.ready() 更简单
    kernel.ready();

    // --- 4. 设置参数并执行内核 ---
    jit_brgemm_params_t args;
    args.ptr_B = compressed_data.data();
    args.bitmask_ptr = bitmask.data();
    args.scratch_buf = actual_output;

    std::cout << "Executing JIT-compiled kernel..." << std::endl;
    generated_func(&args);
    std::cout << "Execution finished." << std::endl;

    // --- 5. 验证结果 ---
    int mismatches = 0;
    for (size_t i = 0; i < output_size; ++i) {
        if (actual_output[i] != expected_output[i]) {
            mismatches++;
        }
    }

    if (mismatches == 0) {
        std::cout << "\nSUCCESS! Decompression matches expected output." << std::endl;
    } else {
        std::cout << "\nFAILURE! Found " << mismatches << " mismatches." << std::endl;

        // 打印第一个不匹配的块
        for (size_t i = 0; i < output_size; i += 64) {
            if (memcmp(actual_output + i, expected_output.data() + i, 64) != 0) {
                print_buffer("Actual Output (Block)", actual_output + i);
                print_buffer("Expected Output (Block)", expected_output.data() + i);
                break;
            }
        }
    }

    // 释放内存
    free(actual_output);

    return mismatches > 0;
}
