# Xbyak Programming Guide

## 1. What Xbyak Is

Xbyak is a header-only JIT assembler for x86 and x86-64. You write C++ that looks close to Intel/MASM syntax, and Xbyak emits machine code into an executable buffer at runtime.

In this workspace the vendored copy lives under `3rdparty/xbyak`.

Useful upstream-local references:

- `3rdparty/xbyak/readme.md`
- `3rdparty/xbyak/doc/usage.md`
- `3rdparty/xbyak/sample/test0.cpp`
- `3rdparty/xbyak/sample/stackframe.cpp`
- `3rdparty/xbyak/sample/memfd.cpp`

Useful oneDNN references in this workspace:

- `oneDNN/src/cpu/x64/jit_generator.hpp`
- `oneDNN/src/cpu/x64/jit_generator.cpp`
- `oneDNN/src/cpu/x64/jit_avx512_sparse_decompress_kernel.hpp`
- `oneDNN/src/cpu/x64/jit_avx512_sparse_decompress_kernel.cpp`
- `oneDNN/src/cpu/README.md`

## 2. Build Setup In This Repo

The minimum headers are:

- `xbyak/xbyak.h`
- `xbyak/xbyak_mnemonic.h`
- `xbyak/xbyak_util.h`

Typical compile command from this repository:

```bash
g++ -O2 -std=c++17 -I../3rdparty/xbyak your_file.cpp -o your_program
```

If you build from a subdirectory, adjust `-I` accordingly. For example, several local tools use:

```bash
g++ -O2 -std=c++11 -I../../3rdparty/xbyak -o example example.cpp
```

## 3. Core Mental Model

The central class is `Xbyak::CodeGenerator`.

You either:

1. Inherit from it and emit instructions in the constructor.
2. Pass a `CodeGenerator` object into a helper function and emit instructions there.

After code emission, obtain a callable function pointer with `getCode<FnType>()`.

Minimal example:

```cpp
#include <xbyak/xbyak.h>

struct Return42 : Xbyak::CodeGenerator {
    Return42() {
        mov(eax, 42);
        ret();
    }
};

int main() {
    Return42 code;
    auto fn = code.getCode<int (*)()>();
    return fn();
}
```

The emitted instructions live in a buffer owned by the `CodeGenerator` unless you provide external memory.

## 4. Syntax Basics

Xbyak syntax is close to Intel syntax, but every instruction is a C++ function call.

```asm
mov eax, ebx
add eax, 5
ret
```

becomes:

```cpp
mov(eax, ebx);
add(eax, 5);
ret();
```

Notes:

- Use `and_()`, `or_()`, `xor_()`, `not_()` instead of keywords that collide with C++ operator names.
- Registers such as `eax`, `rax`, `xmm0`, `zmm1`, `k1` are member objects exposed by `CodeGenerator`.
- If you generate code outside a derived class, use `using namespace Xbyak::util;` for register names.

Example without inheritance:

```cpp
#include <xbyak/xbyak.h>

void genReturn5(Xbyak::CodeGenerator &code) {
    using namespace Xbyak::util;
    code.mov(eax, 5);
    code.ret();
}
```

## 5. Calling Convention Reality

Xbyak only emits instructions. It does not abstract the platform ABI for you.

That means argument registers depend on the target ABI:

- Linux x86-64 System V: integer args typically arrive in `rdi`, `rsi`, `rdx`, `rcx`, `r8`, `r9`
- Windows x64: integer args typically arrive in `rcx`, `rdx`, `r8`, `r9`
- x86-32: args are usually on the stack

The vendored sample `sample/test0.cpp` handles this with preprocessor branches:

```cpp
#ifdef XBYAK32
    mov(eax, ptr [esp + 4]);
#elif defined(XBYAK64_WIN)
    lea(rax, ptr [rcx + y]);
#else
    lea(eax, ptr [edi + y]);
#endif
```

For simple JIT functions, decide the exact function signature first, then map ABI registers explicitly.

Example: add a constant to one `int` argument.

```cpp
struct AddConst : Xbyak::CodeGenerator {
    explicit AddConst(int c) {
#ifdef XBYAK32
        mov(eax, ptr[esp + 4]);
        add(eax, c);
#elif defined(XBYAK64_WIN)
        lea(eax, ptr[rcx + c]);
#else
        lea(eax, ptr[rdi + c]);
#endif
        ret();
    }
};
```

## 6. Registers And Operand Conversion

Xbyak models registers as strongly-typed objects:

- GPRs: `al`, `ax`, `eax`, `rax`, `r8`, `r16`, ...
- SIMD: `xmm0`, `ymm0`, `zmm0`
- Mask registers: `k0` to `k7`
- APX GPRs: `r16` to `r31` and sub-register forms

You can convert register width programmatically:

```cpp
Xbyak::Reg32 r32 = rax.cvt32();
Xbyak::Xmm x = rax.cvt128();
```

This is useful when generating families of instructions from one register base.

## 7. Memory Addressing

Memory operands use `ptr`, `byte`, `word`, `dword`, `qword`, `xword`, `yword`, `zword`.

Examples:

```cpp
mov(eax, ptr[rbx + rcx]);
mov(al, byte[rbx + 3]);
inc(qword[rax]);
mov(ptr[rsp + 8], eax);
```

General form:

```cpp
(ptr|byte|word|dword|qword|xword|yword|zword)[base + index * scale + disp]
```

Rules worth remembering:

- Scale must be `1`, `2`, `4`, or `8`.
- Use an explicit size when the mnemonic cannot infer memory width.
- `ptr[...]` means unsized memory and is only valid when the instruction determines the width.
- RIP-relative addressing is supported in x86-64.

Example:

```cpp
mov(rax, ptr[rip + 32]);
```

## 8. Labels And Control Flow

Xbyak supports string labels and `Xbyak::Label` objects.

### String labels

```cpp
L("loop");
    dec(ecx);
    jnz("loop");
ret();
```

### MASM-style local markers

```cpp
L("@@");
    dec(ecx);
    jnz("@b");
```

- `@b` means the previous `@@`
- `@f` means the next `@@`

### Scoped local labels

For reusable code generators or nested codegen helpers, wrap local labels with:

```cpp
inLocalLabel();
L(".loop");
    dec(ecx);
    jnz(".loop");
outLocalLabel();
```

This pattern is used in `sample/test0.cpp` so multiple instances do not collide on label names.

### Label objects

Use `Xbyak::Label` when you want stronger structure than string names.

```cpp
Xbyak::Label loop, exit;

L(loop);
    test(ecx, ecx);
    jz(exit);
    dec(ecx);
    jmp(loop);
L(exit);
    ret();
```

Check unresolved jumps with `hasUndefinedLabel()`.

## 9. Returning A Function Pointer

Once the code is complete, convert it to a typed function pointer.

```cpp
auto fn = code.getCode<int (*)(int)>();
int result = fn(7);
```

The type must exactly match the ABI and return/argument layout you generated.

Common mistake: generating code that expects one ABI while calling it with a function type that implies another calling convention or argument count.

## 10. Reusing A Generator

`CodeGenerator::reset()` clears the current buffer state so you can emit new code into the same object.

This pattern appears in `sample/test0.cpp`.

```cpp
code.reset();
// emit a new function body
```

If `XBYAK_NO_EXCEPTION` is enabled, `reset()` also clears the sticky error state.

## 11. Code Size, AutoGrow, And Executable Memory

### Default buffer size

The default maximum code size is 4096 bytes.

If you know your JIT body is larger, pass a bigger size:

```cpp
struct BigKernel : Xbyak::CodeGenerator {
    BigKernel() : Xbyak::CodeGenerator(8192) {
        // emit code
    }
};
```

### AutoGrow

If the final size is not known, use `Xbyak::AutoGrow`:

```cpp
struct Kernel : Xbyak::CodeGenerator {
    Kernel() : Xbyak::CodeGenerator(4096, Xbyak::AutoGrow) {
        // emit code
    }
};

Kernel k;
k.readyRE();
auto fn = k.getCode<void (*)()>();
```

Important constraints:

- You must call `ready()` or `readyRE()` before `getCode()`.
- Do not trust addresses returned by `getCurr()` before `ready()`.
- Some address forms are restricted in AutoGrow mode, especially cases that depend on fixed buffer location.

### Read/Write/Exec vs Read/Exec

By default Xbyak may use writable and executable memory while generating code. If you want stricter permissions, use `DontSetProtectRWE` and switch to read/exec after emission:

```cpp
struct SafeCode : Xbyak::CodeGenerator {
    SafeCode() : Xbyak::CodeGenerator(4096, Xbyak::DontSetProtectRWE) {
        mov(eax, 123);
        ret();
    }
};

SafeCode code;
code.setProtectModeRE();
auto fn = code.getCode<int (*)()>();
```

For AutoGrow, prefer `readyRE()`.

## 12. Using Your Own Memory Buffer

You can supply a pre-allocated buffer.

```cpp
alignas(4096) uint8_t buf[8192];

struct Code : Xbyak::CodeGenerator {
    Code() : Xbyak::CodeGenerator(sizeof(buf), buf) {
        mov(eax, 7);
        ret();
    }
};
```

When you supply your own buffer, you are responsible for changing memory permissions with `setProtectModeRE()` or `Xbyak::CodeArray::protect(...)`.

See `sample/test0.cpp` for a working example with aligned user memory.

## 13. Error Handling

Default mode throws `Xbyak::Error` on invalid code generation, for example:

- invalid addressing
- register size mismatch
- undefined label use
- buffer overflow without AutoGrow

Typical pattern:

```cpp
try {
    MyCode code;
    auto fn = code.getCode<int (*)()>();
    printf("%d\n", fn());
} catch (const Xbyak::Error &e) {
    printf("Xbyak error: %s\n", e.what());
}
```

If you compile with `XBYAK_NO_EXCEPTION`, errors become sticky status values:

```cpp
if (Xbyak::GetError()) {
    // handle failure
    Xbyak::ClearError();
}
```

## 14. Utility Layer: `xbyak_util.h`

`xbyak_util.h` provides helpers beyond raw instruction emission.

One practical utility is `Xbyak::util::StackFrame`, which helps set up ABI-friendly function frames and access arguments/temporaries.

Example from the local sample shape:

```cpp
#include <xbyak/xbyak_util.h>

struct Sum3 : Xbyak::CodeGenerator {
    Sum3() {
        Xbyak::util::StackFrame sf(this, 3);
        mov(rax, sf.p[0]);
        add(rax, sf.p[1]);
        add(rax, sf.p[2]);
    }
};
```

See `sample/stackframe.cpp` and `test/sf_test.cpp` for more patterns.

Use `StackFrame` when:

- you want portable argument handling between supported ABIs
- you need callee-save and scratch register organization
- you are generating non-trivial functions with stack temporaries

## 15. SIMD, AVX-512, And APX Forms

Xbyak supports scalar, SSE, AVX, AVX-512, APX, and AVX10.x instruction forms exposed directly as methods.

### AVX example

```cpp
vaddps(xmm1, xmm2, xmm3);
vaddps(xmm2, xmm3, ptr[rax]);
```

### AVX-512 masking and modifiers

```cpp
vaddpd(zmm2 | k5, zmm4, zmm2);
vaddpd(zmm2 | k5 | T_z, zmm4, zmm2 | T_rd_sae);
vmovsd(ptr[rax] | k1, xmm4);
```

Rules:

- `| kN` applies an opmask
- `| T_z` applies zero-masking
- `| T_sae`, `| T_rd_sae`, `| T_ru_sae`, `| T_rz_sae`, `| T_rn_sae` apply rounding or SAE modifiers
- `ptr_b`, `xword_b`, `yword_b`, `zword_b` express broadcast memory forms

Examples:

```cpp
vaddps(zmm1, zmm2, ptr_b[rax + rcx * 8 + 8]);
vcvtpd2dq(xmm19, yword_b[eax + 32]);
```

### APX example

New APX GPRs and modifiers are available in this version of Xbyak:

```cpp
add(r20, r21, r23);
add(r20 | T_nf, r21, r23);
imul(ax | T_zu, cx, 0x1234);
```

That is especially relevant if you are reading modern code in oneDNN or experimenting with new ISA support.

## 16. A Practical First Example

This example sums integers from `0` to `n`, using explicit ABI handling and a local label scope.

```cpp
#include <xbyak/xbyak.h>

struct SumToN : Xbyak::CodeGenerator {
    SumToN() {
        inLocalLabel();

#ifdef XBYAK32
        mov(ecx, ptr[esp + 4]);
#elif defined(XBYAK64_WIN)
    // first integer argument is already in ecx
#else
        mov(ecx, edi);
#endif

        xor_(eax, eax);
        xor_(edx, edx);

        test(ecx, ecx);
        jz(".exit");

        L(".loop");
            add(eax, edx);
            inc(edx);
            cmp(edx, ecx);
            jbe(".loop");

        L(".exit");
            ret();

        outLocalLabel();
    }
};
```

This mirrors the structure of `sample/test0.cpp` and is a good first template for small kernels.

## 17. Common Pitfalls

### 1. Wrong ABI register

The most common failure is reading the first argument from `rdi` on Linux and then testing on Windows where it should be `rcx`.

### 2. Missing memory size

Some instructions require `byte[...]`, `dword[...]`, `qword[...]`, and `ptr[...]` is not enough.

### 3. Forgetting `ready()` in AutoGrow mode

If you use `Xbyak::AutoGrow`, `getCode()` before `ready()` is a bug.

### 4. Executing writable memory forever

For safer JIT behavior, move generated code to read/exec mode with `setProtectModeRE()` or `readyRE()`.

### 5. Mis-typed function pointer

`getCode<int (*)()>()` and `getCode<int (*)(int)>()` are not interchangeable. A wrong type here can look like random runtime corruption.

### 6. Assuming Xbyak validates your algorithm

Xbyak validates encoding constraints, not semantic correctness of your generated function.

## 18. Debug And Profile Workflow In This Repo

Two local helpers are already present:

- `tools/dump_jit_perf` for Linux `perf`
- `tools/dump_jit_vtune` for VTune JIT symbol registration

Typical perf flow:

```bash
perf record -k 1 ./your_program
perf inject --jit -i perf.data -o perf.jit.data
perf report -i perf.jit.data
perf annotate -i perf.jit.data -s your_symbol --stdio
```

This is the right workflow once your kernel works functionally and you want instruction-level hotspots.

## 19. Recommended Learning Path

1. Start with a constant-return function.
2. Add one integer argument and verify ABI register handling.
3. Add a loop with labels.
4. Add memory operands.
5. Move to SIMD instructions.
6. Introduce `StackFrame` once function bodies become non-trivial.
7. Only then add `AutoGrow`, custom allocators, or stricter memory-protection handling.

## 20. Reference Checklist

Before calling generated code, verify:

- the function pointer type matches the emitted ABI
- all labels are resolved
- memory sizes are explicit when needed
- `ready()` or `readyRE()` has been called for AutoGrow
- executable permissions are set correctly
- platform-specific register usage is correct

If you want more examples, the best next files to read are:

- `3rdparty/xbyak/sample/test0.cpp`
- `3rdparty/xbyak/sample/stackframe.cpp`
- `3rdparty/xbyak/sample/ccmp.cpp`
- `3rdparty/xbyak/sample/zero_upper.cpp`
- `3rdparty/xbyak/test/sf_test.cpp`

## 21. How oneDNN Structures Real Xbyak Kernels

The upstream Xbyak docs are good for syntax. oneDNN is useful for learning how to scale that syntax into production JIT kernels.

The central lesson is this: do not let a large kernel become a single constructor full of raw instructions and ad hoc register choices. oneDNN consistently adds a thin JIT framework around Xbyak.

In `oneDNN/src/cpu/x64/jit_generator.hpp`, oneDNN builds a wrapper class `jit_generator_t` on top of `Xbyak::CodeGenerator`. That wrapper contributes several high-value patterns:

- ABI-aware parameter aliases such as `abi_param1`, `abi_param2`, instead of hardcoding `rdi` or `rcx` everywhere.
- Standard `preamble()` and `postamble()` helpers that save callee-saved GPRs, preserve required XMM registers on Windows, restore state, issue `vzeroupper()`, and return.
- ISA-portable helper mnemonics such as `uni_vmovdqu`, `uni_vmovups`, and `uni_vpxor` that choose the best encoding based on the active ISA.
- Safe address builders such as `EVEX_compress_addr`, `make_safe_addr`, `safe_add`, and `safe_sub` so kernels do not silently rely on displacement sizes that later become invalid.
- A deliberate ban on string-literal labels in the wrapper by deleting `L(const char *)`, which pushes authors toward safer label objects.

This is the main oneDNN skill to copy: create a kernel base layer once, then write kernels against that layer.

## 22. oneDNN Skill 1: Separate Kernel Interface From Codegen

oneDNN kernels usually define a small call-parameter struct and treat the generated code as a function over that struct.

Example shape from `jit_avx512_sparse_decompress_kernel.hpp`:

```cpp
struct call_params_t {
    const void *src_ptr;
    const void *bitmask_ptr;
    const void *dst_ptr;
};
```

Why this matters:

- The generated function signature stays stable even when the kernel needs more inputs later.
- The ABI surface becomes one pointer argument instead of several registers with platform-specific mapping.
- Offsets are explicit and easy to audit with `offsetof(...)`.

This pattern is better than exposing a raw multi-argument JIT function when the kernel is non-trivial.

Recommended shape:

```cpp
struct my_kernel_params_t {
    const void *src;
    const void *mask;
    void *dst;
    size_t work;
};

#define GET_OFF(field) offsetof(my_kernel_params_t, field)
```

Then load all runtime state near the start of `generate()` and keep the rest of the kernel focused on actual compute.

## 23. oneDNN Skill 2: Centralize ABI Policy

Your current decompression kernel hardcodes Linux System V by setting:

```cpp
Xbyak::Reg64 param1 = rdi;
```

That is fine for a Linux-only experiment, but it is the first thing that breaks when the code moves.

oneDNN avoids scattering ABI assumptions by defining ABI parameter arrays and aliases in one place. The practical lesson is:

- If the code is experimental and Linux-only, hardcoding `rdi` is acceptable.
- If the code may grow, introduce one alias layer immediately.

For example:

```cpp
#ifdef XBYAK64_WIN
static const Xbyak::Reg64 abi_param1 = rcx;
#else
static const Xbyak::Reg64 abi_param1 = rdi;
#endif
```

Then write the rest of the kernel using `abi_param1`, not `rdi`.

That small discipline pays off quickly when you later refactor helper functions or share the kernel.

## 24. oneDNN Skill 3: Never Hand-Roll Prologue/Epilogue Repeatedly

In your current kernel, the prologue and epilogue are written inline:

- save `r12` to `r15`
- move `k1` to `k4` through `rax` and push them
- restore them manually in reverse order

That works, but it does not scale well. oneDNN moves this logic into `preamble()` and `postamble()` helpers.

What oneDNN's wrapper does for you:

- preserves all required callee-saved GPRs
- preserves Windows-required XMM state
- restores stack and registers consistently
- emits `vzeroupper()` before returning when AVX code was used

The main performance reason for `vzeroupper()` is to avoid AVX to SSE transition penalties when the caller later executes legacy SSE code.

If you are writing more than one kernel, you should strongly consider a tiny local wrapper class:

```cpp
class simple_jit_generator_t : public Xbyak::CodeGenerator {
public:
    using Xbyak::CodeGenerator::CodeGenerator;

    void preamble_simple() {
        push(r12);
        push(r13);
        push(r14);
        push(r15);
    }

    void postamble_simple() {
        vzeroupper();
        pop(r15);
        pop(r14);
        pop(r13);
        pop(r12);
        ret();
    }
};
```

That is not as complete as oneDNN's `jit_generator_t`, but it is already better than copying custom save/restore sequences into every kernel.

## 25. oneDNN Skill 4: Treat Register Allocation As Policy

oneDNN kernels usually do not scatter raw architectural register names through instruction bodies. They declare a register plan up front.

The sparse decompress kernel uses a clear naming scheme:

- `reg_src_ptr`
- `reg_dst_ptr`
- `reg_bitmask_ptr`
- `reg_tmp`
- `reg_popcnt_tmp`
- `reg_popcnt`

and helper accessors such as:

```cpp
Xbyak::Opmask get_opmask(int idx);
Xbyak::Reg64 get_reg_mask_tmp(int idx);
Xbyak::Zmm get_zmm(int idx);
```

This is an important maintainability skill.

For unrolled kernels, instead of naming registers one by one in the hot loop body, write small accessors based on unroll index. That gives you:

- much shorter loop bodies
- fewer copy-paste mistakes
- simpler changes to unroll factor
- easier auditing of register pressure

In your decompression kernel, this is directly applicable. Instead of four separate members

- `reg_comp_mask_tmp1`
- `reg_comp_mask_tmp2`
- `reg_comp_mask_tmp3`
- `reg_comp_mask_tmp4`

consider helper methods:

```cpp
Xbyak::Reg64 get_mask_tmp(int uf) const;
Xbyak::Opmask get_mask(int uf) const;
Xbyak::Zmm get_data_zmm(int uf) const;
```

Once you do that, the main loop becomes a clean `for (int uf = 0; uf < unroll; ++uf)` instead of four manually duplicated blocks.

## 26. oneDNN Skill 5: Build Address Helpers Early

Large JIT kernels often fail in boring ways:

- a displacement grows past the encodable range you expected
- an offset type quietly overflows `int`
- a convenient addressing form stops being valid after a layout change

oneDNN adds explicit helper functions for this.

Important examples from `jit_generator.hpp`:

- `EVEX_compress_addr(...)`
- `maybe_EVEX_compress_addr(...)`
- `make_safe_addr(...)`
- `safe_add(...)`
- `safe_sub(...)`

These helpers encode a practical lesson: do not spread raw `ptr[base + huge_offset]` expressions everywhere in a large generator.

Even if your current offsets are small, a small address helper is worthwhile:

```cpp
Xbyak::Address safe_addr(const Xbyak::Reg64 &base, size_t offt,
        const Xbyak::Reg64 &tmp) {
    if (offt > INT_MAX) {
        mov(tmp, offt);
        return ptr[base + tmp];
    }
    return ptr[base + offt];
}
```

That looks minor, but it prevents a class of future bugs when the kernel evolves.

## 27. oneDNN Skill 6: Wrap ISA Differences Behind `uni_` Helpers

oneDNN's `jit_generator_t` exposes wrappers like:

- `uni_vmovdqu`
- `uni_vmovups`
- `uni_vmovss`
- `uni_vmovsd`
- `uni_vpxor`

These wrappers choose VEX, EVEX, or legacy forms depending on the available ISA and operand type.

Why this is useful:

- your kernel logic becomes about semantics, not encoding trivia
- the same kernel template can often support AVX2 and AVX-512 with less duplication
- instruction spelling becomes consistent across the codebase

If you are only targeting AVX-512 for this repository experiment, direct mnemonics are fine. But once you have multiple ISA targets, you should stop writing raw instruction variants everywhere.

## 28. oneDNN Skill 7: Make Kernel Construction Validate Itself

The oneDNN sparse decompress kernel validates layout assumptions in its constructor before code generation finishes.

Examples:

- checks allowed weight tags
- verifies block sizes match expected layout
- stores constructor status and refuses kernel creation if configuration is invalid

That is a strong pattern for non-trivial JIT code. Separate two classes of failures:

- configuration errors detected before emitting code
- code generation errors detected while emitting code

For your own kernels, prefer this pattern:

```cpp
if (unsupported_layout) {
    // mark failure before generate()
}
```

instead of letting unsupported cases wander deep into the instruction stream and fail later.

## 29. Case Study: oneDNN Sparse Decompress Kernel

`oneDNN/src/cpu/x64/jit_avx512_sparse_decompress_kernel.cpp` is directly relevant to your use case.

Its structure is worth copying almost literally:

1. `preamble()`
2. load runtime pointers from `call_params_t`
3. choose an explicit unroll factor
4. for each unrolled lane:
   - load bitmask into a GPR
   - `popcnt` the mask to get packed-byte count
   - create a load mask
   - masked-load packed bytes with zeroing
   - advance compressed source pointer by popcount
   - move original mask into an opmask
   - `vpexpandb`
   - store the decompressed 64-byte result
5. `postamble()`

The especially useful detail is the load path:

```cpp
vmovdqu8(zmm_reg | load_mask | T_z, ptr[reg_src_ptr]);
add(reg_src_ptr, reg_popcnt);
kmovq(expand_mask, reg_mask_tmp);
vpexpandb(zmm_reg | expand_mask | T_z, zmm_reg);
```

This is more robust than loading a full 64 bytes unmasked and then hoping the packed stream layout always gives safe overread room.

That leads to one of the best practical skills you can take from oneDNN:

- If the compressed stream length is `popcnt(mask)`, prefer a masked load whose active byte count matches that popcount.
- Do not default to unconditional 64-byte loads from a variable-length packed stream unless the surrounding layout explicitly guarantees the overread is harmless.

## 30. Applying oneDNN Lessons To Your Decompression Kernel

Your current kernel is conceptually close to the oneDNN sparse decompress kernel, but oneDNN shows a cleaner shape.

The main improvements to adopt are:

### A. Replace four copy-pasted lanes with indexed helpers

Your current body manually repeats the same logic for lane 0 to lane 3.

oneDNN's approach is better:

- `get_reg_mask_tmp(uf)`
- `get_opmask(uf)`
- `get_zmm(uf)`

Then a single `for (int uf = 0; uf < 4; ++uf)` emits the repeated structure.

### B. Separate load-mask generation from expand-mask generation

The oneDNN kernel distinguishes:

- the mask used to load the compacted packed bytes
- the original bitmask used by `vpexpandb`

That is an important detail. If you load from the compressed stream without a proper load mask, you are implicitly relying on padding or safe overread.

### C. Move prologue/epilogue into helpers

Your inline save and restore sequence works, but the kernel body is carrying too much frame-management noise.

### D. Use a single `call_params_t`

You already do this, which is good. That is aligned with oneDNN practice.

### E. Prefer a named unroll factor and accessors

oneDNN uses `unroll_factor()` and then derives register selection from the lane index. That makes later tuning much easier.

### F. Be explicit about safe compressed-stream reads

This is the most important algorithmic point.

Your current sequence:

```cpp
vmovdqu8(zmm_comp0, ptr[reg_ptr_compressed_src]);
popcnt(reg_popcnt, reg_comp_mask_tmp1);
add(reg_ptr_compressed_src, reg_popcnt);
```

reads 64 bytes regardless of how many packed bytes are actually valid. That may be acceptable only if the compressed buffer is padded enough for every possible overread. oneDNN avoids relying on that.

## 31. Suggested Refactoring Path For Your Kernel

If you want your current kernel to move toward oneDNN quality, the order should be:

1. Introduce `unroll_factor()`, `get_mask_tmp(int)`, `get_kmask(int)`, and `get_zmm(int)` helpers.
2. Replace the four repeated lane blocks with one indexed loop.
3. Add a small prologue/epilogue helper layer.
4. Add safe masked loads for the packed stream.
5. Add comments only at the level of algorithm phases, not per instruction.
6. Add validation for block sizes, chunk sizes, and pointer assumptions in the constructor.

That order improves correctness first, then maintainability, then portability.

## 32. What To Learn From oneDNN, Specifically

The most useful skills to extract are not individual instructions. They are engineering habits:

- define a stable runtime call-parameter struct
- isolate ABI assumptions behind aliases
- centralize prologue and epilogue logic
- encode register allocation as named policy
- factor repeated lanes with indexed accessors
- create helper APIs for address safety
- use ISA abstraction wrappers when kernels must span multiple ISAs
- validate configuration before generating code
- make compressed-stream loads reflect actual valid-byte count

These are the habits that turn a working Xbyak prototype into something you can still debug three months later.