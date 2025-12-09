#include <iostream>
#include <vector>
#include <cstdint>
#include <cstring>
#include <cassert>
#include <limits.h>
#include "xbyak/xbyak.h"
#include "xbyak/xbyak_util.h"

// 结构体定义保持不变，用于参数传递
struct call_params_t {
    const void *src_ptr;      // 压缩后的数据源指针
    const void *bitmask_ptr;  // 稀疏位掩码指针 (uint64_t 数组)
    const void *dst_ptr;      // 解压缩后的目标指针
};

// 宏定义用于获取结构体成员的偏移量
#define GET_OFF(field) offsetof(call_params_t, field)

// 使用 Xbyak::CodeGenerator 替换 jit_generator_t
class SparseDecompressKernel : public Xbyak::CodeGenerator {
public:
    // 固定的块大小参数
    const int a_outter_blk_sz_ = 16;
    const int a_inner_blk_sz_ = 4;
    const int b_blk_sz_ = 64; // 从原代码逻辑推断
    const int blk_sz_ = a_outter_blk_sz_ * b_blk_sz_ * a_inner_blk_sz_; // 16 * 64 * 4 = 4096
    const int nblks_to_decompress_ = 1; // 简化为只处理一个 K 块

private:
    // 寄存器映射 (与原代码保持一致)
    const Xbyak::Reg64 reg_src_ptr = r8;
    const Xbyak::Reg64 reg_dst_ptr = r9;
    const Xbyak::Reg64 reg_bitmask_ptr = r10;
    const Xbyak::Reg64 reg_tmp = r11;
    const Xbyak::Reg64 reg_popcnt_tmp = r12;
    const Xbyak::Reg64 reg_popcnt = rcx; // 用于 popcnt 结果和 shl 计数

    // Unroll Factor (UF) 辅助寄存器和 ZMM/Opmask
    const int unroll_factor = 4;
    const Xbyak::Reg64 reg_mask_tmp_arr[4] = {r13, r14, r15, rax};
    const Xbyak::Zmm zmm_arr[4] = {Xbyak::Zmm(25), Xbyak::Zmm(26), Xbyak::Zmm(27), Xbyak::Zmm(28)};
    const Xbyak::Opmask k_arr[4] = {k1, k2, k3, k4};

    Xbyak::util::Cpu m_cpu;

    void prepare_load_mask(const Xbyak::Opmask &opmask) {
        // 实现原 get_load_mask 的功能：将 reg_popcnt 中的值转换为一个低位设置了 'reg_popcnt' 位的 Opmask。
        // 由于 shl(64) == shl(0) 的问题，需要拆分移位操作。

        // 备份 reg_popcnt (实际需要加载的字节数)
        mov(reg_popcnt_tmp, reg_popcnt); 
        
        // 1. 计算掩码：2^N - 1
        mov(reg_tmp, 1);
        
        // 2. 分两次移位 (shift = N/2)
        shr(reg_popcnt, 1); // reg_popcnt = N / 2
        shl(reg_tmp, reg_popcnt.cvt8());
        shl(reg_tmp, reg_popcnt.cvt8());

        // 3. 计算尾数移位 (shift_tail = N % 2)
        mov(reg_popcnt, reg_popcnt_tmp); // 恢复 N
        and_(reg_popcnt, 1); // reg_popcnt = N % 2

        // 4. 应用尾数移位
        shl(reg_tmp, reg_popcnt.cvt8());

        // 5. 减 1 得到掩码 (2^N - 1)
        sub(reg_tmp, 1);

        // 6. 恢复 reg_popcnt 并写入 Opmask
        mov(reg_popcnt, reg_popcnt_tmp); // 恢复 N (供 add(reg_src_ptr, reg_popcnt) 使用)
        kmovq(opmask, reg_tmp);
    }

    // 核心代码生成逻辑
    void generate_code() {
        // Xbyak 约定：param1 是第一个参数（本例中为 call_params_t*）
        Xbyak::Label loop_b_start, loop_b_end;

        // 1. 设置调用约定 (Windows: rcx/rdx/r8/r9; Linux: rdi/rsi/rdx/rcx)
        // 假设使用 Linux 64-bit ABI (param1 in rdi) 或 Windows 64-bit ABI (param1 in rcx)
        // 为了跨平台兼容性，使用 push/pop 来保存和恢复 caller-save 寄存器
        
        // Xbyak::CodeGenerator 默认有 preamble/postamble，但为了纯粹性，手动实现
        // 手动保存 caller-save 寄存器 r8, r9, r10, r11, r12, r13, r14, r15, rax, rcx
        push(r13); push(r14); push(r15); push(rax); push(rcx); 
        push(r8); push(r9); push(r10); push(r11); push(r12); // 保存 callee-save/temp 

        // 2. 从 call_params_t* 中加载指针
        // 假设 call_params_t* 已经在 rdi (Linux) 或 rcx (Windows)
#ifdef _WIN64
        const Xbyak::Reg64 reg_params = rcx;
#else // Linux
        const Xbyak::Reg64 reg_params = rdi;
#endif

        mov(reg_bitmask_ptr, ptr[reg_params + GET_OFF(bitmask_ptr)]);
        mov(reg_dst_ptr, ptr[reg_params + GET_OFF(dst_ptr)]);
        mov(reg_src_ptr, ptr[reg_params + GET_OFF(src_ptr)]);

        // 3. 循环 nblks_to_decompress_ 次 (此处简化为 nblks_to_decompress_ = 1，因此省略外层循环)
        const int nbytes_per_load = 64; // ZMM 寄存器的字节数

        const int blk_offset = 0; // 块偏移量
        const int bitmask_off = blk_offset / CHAR_BIT; // 位掩码偏移量

        // 4. i 循环：i < b_blk_sz_ (64)，步长为 unroll_factor (4)
        Xbyak::Reg64 reg_i = r14;
        mov(reg_i, 0); // i = 0

    L("i_loop_start");
        cmp(reg_i, b_blk_sz_);
        jge("i_loop_end");

        for (int uf = 0; uf < unroll_factor; uf++) {
            auto reg_mask_tmp = reg_mask_tmp_arr[uf];
            auto zmm_reg = zmm_arr[uf];
            auto opmask = k_arr[uf];

            // A. Load Bitmask & Popcount
            // 访问的掩码地址: bitmask_ptr + (i + uf) * sizeof(uint64_t) + bitmask_off
            mov(reg_mask_tmp, 
                ptr[reg_bitmask_ptr + reg_i + (uf * sizeof(uint64_t)) + bitmask_off]); // reg_i 已经是字节偏移，所以直接加

            popcnt(reg_popcnt, reg_mask_tmp); // rcx = 稀疏元素个数

            // B. Prepare Load Mask (k1-k4)
            prepare_load_mask(opmask); // 内部会用到 r11, r12, rcx

            // C. Load Packed Data
            vmovdqu8(zmm_reg | opmask | T_z, ptr[reg_src_ptr]);
            add(reg_src_ptr, reg_popcnt); // 源指针前进 reg_popcnt 个字节

            // D. Prepare Expand Mask (k1-k4)
            kmovq(opmask, reg_mask_tmp); // 将原始 64-bit 掩码写入 Opmask

            // E. Decompress (Expand)
            vpexpandb(zmm_reg | opmask | T_z, zmm_reg);

            // F. Store Decompressed Data
            // 目标地址: dst_ptr + blk_offset + (i + uf) * nbytes_per_load
            mov(reg_tmp, reg_i);
            add(reg_tmp, uf * nbytes_per_load);
            vmovdqu8(ptr[reg_dst_ptr + reg_tmp], zmm_reg); 
        }
        
        add(reg_i, unroll_factor * sizeof(uint64_t)); // reg_i 增加 4 * 8 = 32 bytes (64 / 4 * 4 * 8 / 16)
        jmp("i_loop_start");
    L("i_loop_end");

        // 5. 恢复寄存器
        pop(r12); pop(r11); pop(r10); pop(r9); pop(r8);
        pop(rcx); pop(rax); pop(r15); pop(r14); pop(r13);
        
        ret();
    }

public:
    SparseDecompressKernel()
        // CodeGenerator 构造函数: (Code Size, JIT mode)
        : Xbyak::CodeGenerator(4096, 0) 
    {
        // 检查 CPU feature 是否支持 AVX512 (简化检查)
        if (!m_cpu.has(Xbyak::util::Cpu::tAVX512F)) {
            throw std::runtime_error("AVX512 is not supported.");
        }
        
        generate_code(); // 调用核心生成逻辑
    }

    // 获取 JIT 函数指针
    void (*get_jit_func() const)(call_params_t *) {
        return reinterpret_cast<void (*)(call_params_t *)>(getCode());
    }
};

#undef GET_OFF

// --- 测试代码 ---
void run_test() {
    std::cout << "--- 稀疏解压缩 JIT Kernel 测试 ---" << std::endl;

    // 1. 初始化 Kernel
    SparseDecompressKernel kernel;
    auto jit_func = kernel.get_jit_func();

    // 2. 设置参数
    const int B_BLK = kernel.b_blk_sz_; // 64
    const int BYTES_PER_ROW = 64; // ZMM load size (64 bytes)
    const int NUM_ROWS = B_BLK; // 64 行
    const int BITMASK_SIZE = NUM_ROWS * sizeof(uint64_t);

    // a. 稀疏位掩码 (64行，每行一个 uint64_t 掩码)
    std::vector<uint64_t> bitmask(NUM_ROWS, 0);
    // 示例掩码: 
    // row 0: 0xF (低 4 位稀疏) -> popcnt=4
    // row 1: 0xFF (低 8 位稀疏) -> popcnt=8
    // row 2: 0x0 (全稀疏) -> popcnt=0
    // row 3: 0xFFFFFFFFFFFFFFFF (全密) -> popcnt=64
    // row 4: 0x8000000000000000 (最高 1 位稀疏) -> popcnt=1

    bitmask[0] = 0xF; 
    bitmask[1] = 0xFF;
    bitmask[2] = 0x0;
    bitmask[3] = 0xFFFFFFFFFFFFFFFF;
    bitmask[4] = 0x8000000000000000;

    // b. 压缩后的源数据 (Src)
    // 需要空间容纳所有 popcnt 值的和
    size_t total_packed_size = 0;
    for (uint64_t mask : bitmask) {
        total_packed_size += __builtin_popcountll(mask);
    }

    std::vector<uint8_t> src_data(total_packed_size);
    // 填充测试数据: 1, 2, 3, 4, 5, ...
    for (size_t i = 0; i < total_packed_size; ++i) {
        src_data[i] = (uint8_t)(i + 1); 
    }

    // c. 解压缩后的目标数据 (Dst) - 稀疏矩阵
    const int DST_SIZE = NUM_ROWS * BYTES_PER_ROW; // 64 * 64 = 4096 字节
    std::vector<uint8_t> dst_data(DST_SIZE, 0xCC); // 填充 CC 方便查看未写入的区域

    // d. JIT 参数结构体
    call_params_t params = {
        src_data.data(),
        bitmask.data(),
        dst_data.data()
    };
    
    // 3. 执行 JIT Kernel
    std::cout << "-> 执行 JIT Kernel..." << std::endl;
    jit_func(&params);
    std::cout << "-> 执行完成。" << std::endl;

    // 4. 验证结果
    std::cout << "--- 验证结果 ---" << std::endl;
    int src_idx = 0;
    bool success = true;

    for (int i = 0; i < NUM_ROWS; ++i) {
        uint64_t mask = bitmask[i];
        int popcnt = __builtin_popcountll(mask);
        
        std::cout << "Row " << i << ": Mask=0x" << std::hex << mask << ", Popcnt=" << std::dec << popcnt << std::endl;

        for (int j = 0; j < BYTES_PER_ROW; ++j) {
            bool is_active = (mask >> j) & 0x1;
            uint8_t expected_val;

            if (is_active) {
                // 预期值应该是压缩数据中的下一个值
                expected_val = src_data[src_idx];
                src_idx++;
            } else {
                // 稀疏位置应为 0 (T_z 零掩码作用)
                expected_val = 0; 
            }

            uint8_t actual_val = dst_data[i * BYTES_PER_ROW + j];

            if (actual_val != expected_val) {
                std::cerr << "FAIL at Dst[" << i << ", " << j << "]: Expected 0x" << std::hex << (int)expected_val 
                          << ", Actual 0x" << (int)actual_val << std::endl;
                success = false;
                // 为了避免输出过多错误，只展示前几个失败
                if (src_idx > 100) return; 
            } else if (is_active && j < 4) {
                 // 打印前几个活跃元素以供查看
                 std::cout << "  Active[" << j << "]: Value=" << (int)actual_val << std::endl;
            }
        }
    }

    if (success) {
        std::cout << "\n✅ 所有验证通过！稀疏解压缩成功。" << std::endl;
    } else {
        std::cout << "\n❌ 验证失败！请检查 JIT 代码中的寄存器使用和寻址逻辑。" << std::endl;
    }
}

int main() {
    try {
        run_test();
    } catch (const std::exception& e) {
        std::cerr << "Caught exception: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}