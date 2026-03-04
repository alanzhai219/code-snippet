#include <stddef.h>
#include "xbyak/xbyak.h"

struct jit_decomp_params_t_opt {
    const void* compressed_buf;
    const void* bitmask_ptr;
    void* decomp_buf;
};

#define GET_OFF_OPT(field) (offsetof(jit_decomp_params_t_opt, field))

class jit_decompress_kernel_t_opt : public Xbyak::CodeGenerator {
public:
    Xbyak::Reg64 param1 = rdi;

    Xbyak::Reg64 compressed_ptr = r8;
    Xbyak::Reg64 reg_ptr_compressed_bitmask = r9;
    Xbyak::Reg64 reg_ptr_decomp_dst = r10;
    Xbyak::Reg64 reg_ptr_compressed_src = r11;

    Xbyak::Reg64 reg_comp_mask_tmp1 = rax;
    Xbyak::Reg64 reg_comp_mask_tmp2 = rcx;
    Xbyak::Reg64 reg_comp_mask_tmp3 = rdx;
    Xbyak::Reg64 reg_comp_mask_tmp4 = rsi;
    Xbyak::Reg64 reg_popcnt = rdi;
    Xbyak::Reg64 reg_ptr_compressed_src_align = r15;

    Xbyak::Opmask reg_comp_mask1 = k1;
    Xbyak::Opmask reg_comp_mask2 = k2;
    Xbyak::Opmask reg_comp_mask3 = k3;
    Xbyak::Opmask reg_comp_mask4 = k4;

    Xbyak::Zmm zmm_comp0 = zmm0;
    Xbyak::Zmm zmm_comp1 = zmm1;
    Xbyak::Zmm zmm_comp2 = zmm2;
    Xbyak::Zmm zmm_comp3 = zmm3;

    int blocks_;

    jit_decompress_kernel_t_opt(int blocks) : Xbyak::CodeGenerator(4096 * 4096), blocks_(blocks) {
        generate();
    }

    void generate() {
        push(r12);
        push(r13);
        push(r14);
        push(r15);

        mov(compressed_ptr, ptr[param1 + GET_OFF_OPT(compressed_buf)]);
        mov(reg_ptr_compressed_bitmask, ptr[param1 + GET_OFF_OPT(bitmask_ptr)]);
        mov(reg_ptr_decomp_dst, ptr[param1 + GET_OFF_OPT(decomp_buf)]);
        lea(reg_ptr_compressed_src, ptr[compressed_ptr]);

        Xbyak::Label loop_blocks;
        Xbyak::Label end_blocks;

        mov(r12, blocks_);
        test(r12, r12);
        jz(end_blocks, T_NEAR);

        L(loop_blocks);
        mov(r14, reg_ptr_decomp_dst);
        for (int i = 0; i < 16; ++i) {
            const int bm_off = i * 32;
            const int dst_off = i * 256;

            mov(reg_comp_mask_tmp1, ptr[reg_ptr_compressed_bitmask + bm_off + 0]);
            kmovq(reg_comp_mask1, reg_comp_mask_tmp1);

            mov(reg_comp_mask_tmp2, ptr[reg_ptr_compressed_bitmask + bm_off + 8]);
            kmovq(reg_comp_mask2, reg_comp_mask_tmp2);

            mov(reg_comp_mask_tmp3, ptr[reg_ptr_compressed_bitmask + bm_off + 16]);
            kmovq(reg_comp_mask3, reg_comp_mask_tmp3);

            mov(reg_comp_mask_tmp4, ptr[reg_ptr_compressed_bitmask + bm_off + 24]);
            kmovq(reg_comp_mask4, reg_comp_mask_tmp4);

            vpexpandb(zmm_comp0 | reg_comp_mask1 | T_z, ptr[reg_ptr_compressed_src]);
            popcnt(reg_popcnt, reg_comp_mask_tmp1);
            add(reg_ptr_compressed_src, reg_popcnt);
            vmovdqu8(ptr[r14 + dst_off + 0], zmm_comp0);

            vpexpandb(zmm_comp1 | reg_comp_mask2 | T_z, ptr[reg_ptr_compressed_src]);
            popcnt(reg_popcnt, reg_comp_mask_tmp2);
            add(reg_ptr_compressed_src, reg_popcnt);
            vmovdqu8(ptr[r14 + dst_off + 64], zmm_comp1);

            vpexpandb(zmm_comp2 | reg_comp_mask3 | T_z, ptr[reg_ptr_compressed_src]);
            popcnt(reg_popcnt, reg_comp_mask_tmp3);
            add(reg_ptr_compressed_src, reg_popcnt);
            vmovdqu8(ptr[r14 + dst_off + 128], zmm_comp2);

            vpexpandb(zmm_comp3 | reg_comp_mask4 | T_z, ptr[reg_ptr_compressed_src]);
            popcnt(reg_popcnt, reg_comp_mask_tmp4);
            add(reg_ptr_compressed_src, reg_popcnt);
            vmovdqu8(ptr[r14 + dst_off + 192], zmm_comp3);
        }

        add(reg_ptr_compressed_bitmask, 512);

        mov(reg_ptr_compressed_src_align, reg_ptr_compressed_src);
        sub(reg_ptr_compressed_src_align, compressed_ptr);
        neg(reg_ptr_compressed_src_align);
        and_(reg_ptr_compressed_src_align, 0x3f);
        add(reg_ptr_compressed_src, reg_ptr_compressed_src_align);

        add(reg_ptr_decomp_dst, 4096);
        dec(r12);
        jnz(loop_blocks, T_NEAR);

        L(end_blocks);
        pop(r15);
        pop(r14);
        pop(r13);
        pop(r12);
        ret();
    }
};
