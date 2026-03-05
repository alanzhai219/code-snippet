# JIT 何时能超越 AVX-512 Intrinsics

## 背景

在通用解压缩场景（动态位掩码）下，AVX-512 intrinsics 内核比最优的通用 JIT 内核（前缀和寻址）快约 1.3%。本文档描述了 JIT **反超**的场景——在特定条件下，JIT 性能超越 intrinsics **1.36–1.62 倍**。

---

## 核心思想：运行时特化

在 LLM 推理中，权重稀疏掩码在**模型加载时就已确定**，之后在每次推理调用中被重复使用。JIT 编译器可以在代码生成阶段检查实际的掩码值，从而生成静态（AOT）编译器无法产生的代码：

| 运行时被消除的开销 | 实现方式 |
|-------------------|---------|
| 所有 `popcnt` 指令 | 源偏移量作为立即数常量预计算 |
| 所有指针推进 `add` | 地址变为 `[base + 立即数]` — 无串行依赖 |
| 所有位掩码内存加载 | 掩码值嵌入为 64 位立即数 |
| 所有对齐运算 | 块间填充在 JIT 编译时预计算 |
| 零块的冗余 expand | 替换为 `vpxord + store`（2 条指令） |
| 稠密块的冗余 expand | 替换为 `vmovdqu8` 拷贝（2 条指令） |

---

## 生成代码对比

### 通用内核（intrinsics 或通用 JIT）— 每个 chunk：6 条指令

```asm
mov  rax, [bitmask + runtime_off]  ; 内存加载（位掩码）
kmovq k1, rax                      ; k-mask 设置
vpexpandb zmm{k1}{z}, [src]        ; 扩展加载（数据依赖的地址）
popcnt rax, rax                    ; 3 周期延迟
add  src, rax                      ; 依赖 popcnt → 串行链
vmovdqu8 [dst + off], zmm          ; 存储
```

### JIT 特化版 — 部分填充 chunk：4 条指令，0 地址计算周期

```asm
mov  rax, 0x0101...                ; 立即数掩码（无内存加载）
kmovq k1, rax                      ; k-mask 设置
vpexpandb zmm{k1}{z}, [base + IMM] ; 静态地址（无依赖）
vmovdqu8 [dst + IMM], zmm          ; 存储
```

### JIT 特化版 — 全零 chunk：2 条指令

```asm
vpxord zmm, zmm, zmm               ; 寄存器清零
vmovdqu8 [dst + IMM], zmm          ; 存储（无需读取压缩数据）
```

### JIT 特化版 — 全稠密 chunk：2 条指令

```asm
vmovdqu8 zmm, [base + IMM]         ; 直接 64 字节拷贝（无需 k-mask）
vmovdqu8 [dst + IMM], zmm          ; 存储
```

---

## 基准测试配置

```bash
g++ decomp_bench_jit_advantage.cpp -I../../3rdparty/xbyak -march=native -O2 -o decomp_bench_jit_advantage
./decomp_bench_jit_advantage
```

- **数据规模**：4 个块 × 4096 字节，500K 次迭代，2000 次预热
- **对比内核**：AVX-512 Intrinsics（通用） / JIT 通用版（前缀和） / JIT 特化版（掩码感知）

---

## 测试结果

### 场景 A — 随机 70% 稀疏度（非结构化剪枝）

所有 256 个 chunk 均为部分填充（无全零/全稠密捷径可用）。

| 内核 | 微秒/调用 | 对比 Intrinsics |
|------|----------|----------------|
| AVX-512 Intrinsics | ~0.204 | 基准线 |
| JIT 通用版 | ~0.202 | ~1.0× |
| JIT 特化版 | ~0.202 | ~1.0×（持平） |

**分析**：在均匀随机稀疏度下，每个 chunk 都是部分填充。JIT 的唯一优势在于静态寻址（消除了 `popcnt`/`add`），但在小数据量下，乱序执行引擎可以隐藏该延迟。结果：持平。

### 场景 B — 结构化稀疏度（30% 全零块 + 10% 全稠密块）

这是通道剪枝或块稀疏 LLM 权重的典型模式。

| 内核 | 微秒/调用 | 对比 Intrinsics |
|------|----------|----------------|
| AVX-512 Intrinsics | ~0.202 | 基准线 |
| JIT 通用版 | ~0.220 | 0.92×（更慢！） |
| **JIT 特化版** | **~0.149** | **1.36–1.62× 更快 ★** |

生成代码中的 chunk 分布：

| chunk 类型 | 数量 | 占比 | 每 chunk 指令数 |
|-----------|------|------|----------------|
| 全零 (mask=0) | 69/256 | 27% | 2（vpxord + store） |
| 全稠密 (mask=~0) | 29/256 | 11% | 2（vmovdqu8 拷贝） |
| 部分填充 | 158/256 | 62% | 4（mov+kmovq+vpexpandb+store） |

**JIT 获胜原因**：Intrinsics 对所有 chunk 都执行 `vpexpandb + popcnt + add`（6 条指令）。而特化 JIT 对 38% 的 chunk（全零 + 全稠密）仅使用 2 条指令，完全消除了压缩数据读取和 k-mask 逻辑。

---

## 对 LLM 推理的意义

```
模型加载                              推理（数百万次调用）
   │                                        │
   ├─ 解析权重                               ├─ 调用 jit_func(&args)
   ├─ 提取稀疏掩码                            │   └─ 每次调用 0.149 微秒
   ├─ JIT 编译特化内核                         │       （无 popcnt，无位掩码加载，
   │   └─ 编译开销：~微秒级                    │        无指针运算）
   └─ 就绪                                   └─ ...
```

JIT 编译开销（~微秒）在**数百万次推理调用中被摊销**。每次调用节省约 33% 的指令。

---

## 何时使用哪种内核

| 场景 | 最优内核 | 原因 |
|------|---------|------|
| 固定掩码，大量重复使用 | **JIT 特化版** | 编译开销可摊销，静态地址，死代码消除 |
| 动态掩码，大块数据 | AVX-512 Intrinsics | 无 JIT 编译开销，编译器调度优良 |
| 动态掩码，通用场景 | JIT 通用版（前缀和） | 与 intrinsics 差距仅 1.3% |

---

## 核心结论

> **JIT 的真正力量不在于匹配编译器的输出——而在于做编译器根本做不到的事**：基于运行时数据特化代码。

当位掩码值从运行时变量变为编译时常量，JIT 能够消除整类指令（`popcnt`、指针运算、位掩码加载），并为全零/全稠密 chunk 生成经过死代码消除的特化路径。这是运行时代码生成的典型应用场景。

---

## 深入理解：AOT vs JIT 的能力边界

### AOT 与 JIT 看到的世界

AOT（静态/提前编译器，如 GCC、Clang）在编译时只能看到**代码**，看不到**数据**。JIT 编译器在运行时同时看到**代码和数据**，可以把数据"烧进"生成的机器码中。

```
AOT 编译器看到的：                    JIT 编译器看到的：
┌──────────────┐                    ┌──────────────┐
│   源代码      │                    │   源代码      │
│  (变量是符号)  │                    │  (变量是符号)  │
└──────┬───────┘                    └──────┬───────┘
       │                                   │
       │ 编译                               │ + 运行时数据
       ▼                                   ▼
┌──────────────┐                    ┌──────────────────────┐
│  通用机器码    │                    │  mask = 0xFF00FF00... │
│  (必须处理所有  │                    │  popcnt = 32          │
│   可能的输入)  │                    │  offset = 已知常量     │
└──────────────┘                    └──────────┬───────────┘
                                              │ 生成
                                              ▼
                                   ┌──────────────────────┐
                                   │  专用机器码             │
                                   │  (只处理这一种输入)      │
                                   └──────────────────────┘
```

---

### 示例 1：本项目中的 vpexpandb 解压缩

**AOT 编译器生成的代码**（必须处理任意掩码）：

```cpp
// 编译器不知道 mask 的值，必须在运行时：
// 1. 从内存加载 mask
// 2. 计算 popcnt（确定多少非零字节）
// 3. 用 popcnt 推进源指针
// 4. 执行 vpexpandb
for (int cl = 0; cl < 64; cl++) {
    uint64_t mask = bitmask[cl];           // 编译器不知道这是 0、~0、还是其他
    zmm = _mm512_maskz_expandloadu_epi8(mask, src);
    _mm512_storeu_si512(dst + cl*64, zmm);
    src += _mm_popcnt_u64(mask);           // 必须运行时计算
}
```

编译器**必须**为每个 chunk 生成完整的 6 条指令序列，因为它不知道 mask 的值：

```asm
mov   rax, [bitmask + cl*8]    ; ← 不能省：mask 未知
kmovq k1, rax                  ; ← 不能省：需要设置 k-mask
vpexpandb zmm, [src]{k1}{z}    ; ← 不能省：必须执行 expand
popcnt rax, rax                ; ← 不能省：偏移量未知
add   src, rax                 ; ← 不能省：指针依赖 popcnt
vmovdqu8 [dst], zmm            ; ← 不能省：必须存储
```

**JIT 编译器看到实际数据后**：

```cpp
// JIT 在代码生成时知道：bitmask[17] = 0x0000000000000000（全零）
// 所以这个 chunk：
//   - 不需要从压缩流读取任何数据
//   - 不需要设置 k-mask
//   - 不需要 vpexpandb
//   - 不需要 popcnt
//   - 源指针偏移量 = 0（编译时已知）
```

生成的代码只有 **2 条指令**：

```asm
vpxord zmm0, zmm0, zmm0       ; 清零
vmovdqu8 [dst + 1088], zmm0   ; 存储（偏移量是立即数）
```

**对比**：同一个 chunk，AOT = 6 条指令 + 串行依赖链，JIT = 2 条指令 + 零依赖。

---

### 示例 2：常量传播 — if 分支消除

**AOT 代码**：

```cpp
void process(int mode, float* data, int n) {
    for (int i = 0; i < n; i++) {
        if (mode == 1)      data[i] *= 2.0f;
        else if (mode == 2) data[i] += 1.0f;
        else                data[i] = 0.0f;
    }
}
```

AOT 编译器**不知道 mode 的值**，必须在循环内保留所有 3 个分支：

```asm
.loop:
    cmp  edi, 1          ; mode == 1?
    je   .mul
    cmp  edi, 2          ; mode == 2?
    je   .add
    ; else: zero
    ...
    jmp  .next
.mul:
    ...
    jmp  .next
.add:
    ...
.next:
    dec  ecx
    jnz  .loop
```

每次迭代：2 次比较 + 1 次跳转 = **分支预测压力 + 指令膨胀**。

**JIT 知道 mode=2**，直接生成：

```asm
.loop:
    vaddps zmm0, zmm0, zmm_one   ; 只有 add 路径，无分支
    dec  ecx
    jnz  .loop
```

消除了：所有比较指令、所有分支跳转、不可达代码（mode 1 和 else 路径）。

---

### 示例 3：循环次数已知 — 完全展开

**AOT 代码**：

```cpp
void memcpy_blocks(void* dst, void* src, int n_blocks) {
    for (int i = 0; i < n_blocks; i++)
        memcpy(dst + i*64, src + i*64, 64);
}
```

编译器不知道 `n_blocks`，必须生成通用循环（计数器、比较、跳转）。

**JIT 知道 n_blocks = 3**，直接展开：

```asm
vmovdqu64 zmm0, [src]
vmovdqu64 [dst], zmm0
vmovdqu64 zmm1, [src + 64]
vmovdqu64 [dst + 64], zmm1
vmovdqu64 zmm2, [src + 128]
vmovdqu64 [dst + 128], zmm2
ret
```

消除了：循环计数器、比较、跳转、循环归纳变量。

---

### 示例 4：稀疏矩阵乘法 — 结构已知

**AOT 代码**（通用 CSR SpMV）：

```cpp
// 编译器不知道每行有多少非零元素
for (int row = 0; row < N; row++) {
    float sum = 0;
    for (int j = row_ptr[row]; j < row_ptr[row+1]; j++) {  // 内循环次数未知
        sum += values[j] * x[col_idx[j]];                   // 间接寻址
    }
    y[row] = sum;
}
```

每次迭代需要：加载 `row_ptr`、比较、加载 `col_idx`（间接寻址）、加载 `values`。

**JIT 看到实际矩阵后**（例如某行只有 3 个非零元素在列 5、12、37）：

```asm
; row 42: 3 non-zeros at columns 5, 12, 37
vmulss xmm0, [values + 408], [x + 20]       ; val[i] * x[5]  — 直接地址
vfmadd231ss xmm0, [values + 412], [x + 48]  ; += val[i+1] * x[12]
vfmadd231ss xmm0, [values + 416], [x + 148] ; += val[i+2] * x[37]
vmovss [y + 168], xmm0
```

消除了：`row_ptr` 加载、循环比较跳转、`col_idx` 间接寻址（列索引直接编码为偏移量）。

---

### AOT vs JIT 能力总结

| 优化 | AOT 编译器 | JIT 编译器 |
|------|-----------|-----------|
| 循环展开（次数未知） | ❌ 只能猜测或部分展开 | ✅ 知道确切次数，完全展开 |
| 分支消除（条件未知） | ❌ 必须保留所有分支 | ✅ 只保留实际执行的路径 |
| 常量传播（值未知） | ❌ 变量必须在寄存器中 | ✅ 值嵌入为立即数 |
| 死代码消除（依赖数据） | ❌ 不知道哪些代码不可达 | ✅ 直接跳过不可达路径 |
| 地址计算消除 | ❌ 偏移量运行时计算 | ✅ 偏移量编码为立即数 |
| 数据感知指令选择 | ❌ 同一指令处理所有输入 | ✅ 全零用 vpxord，全满用 vmovdqu8 |

**根本原因**：AOT 编译器的输入是**程序**（有限的静态信息），JIT 编译器的输入是**程序 + 数据**（完整的运行时信息）。数据越"有结构"（有规律、有特殊值），JIT 的优势越大。在本项目中，结构化稀疏度下 JIT 特化版比 intrinsics 快 **1.36–1.62×**，正是因为 38% 的 chunk 是全零或全稠密，JIT 为它们生成了完全不同的指令序列。

---

## JIT 如何"知道"常量值？— 机制详解

一个常见的疑问是：JIT 怎么"知道"数据的值？答案很简单——**你在 C++ 构造函数中把数据传进去，代码生成器像普通 C++ 代码一样读取这些值，然后把它们作为立即数嵌入到机器码中**。

### 两个"时间维度"

JIT 的核心在于理解有两个时间维度：

```
时间 1：JIT 编译时（C++ 代码运行）          时间 2：运行时（机器码执行）
─────────────────────────────────          ───────────────────────────────

bitmask_ = [0x00, 0xFF.., 0x0101..]       （bitmask 数组不被访问）

for chunk in 0..63:                       （无循环 — 已完全展开）
  mask = bitmask_[chunk]   // C++ 读取
  popcnt = popcount(mask)  // C++ 计算    （无 popcnt 指令）
  src_off += popcnt        // C++ 加法    （无 add 指令）

  if mask == 0:            // C++ 分支
    发射: vpxord + store                   vpxord zmm0, zmm0, zmm0
                                           vmovdqu8 [dst+1088], zmm0

  elif mask == ~0:         // C++ 分支
    发射: vmovdqu8 加载 + store             vmovdqu8 zmm0, [src+320]
                                           vmovdqu8 [dst+1152], zmm0

  else:
    发射: mov 立即数 + kmovq + vpexpandb    mov rax, 0x0101010101010101
                                           kmovq k1, rax
                                           vpexpandb zmm0{k1}{z}, [src+352]
                                           vmovdqu8 [dst+1216], zmm0
```

关键：**C++ 的 `for` 循环、`if` 分支、`src_off += popcnt` 全部在构造时执行完毕**。它们的输出是机器码字节。当 `jit_func(&args)` 被调用时，这些循环和分支已经不存在——只剩下发射出的指令。

### 本项目中的实际代码

```cpp
class jit_decompress_specialized_t : public Xbyak::CodeGenerator {
    int blocks_;
    const uint64_t* bitmask_;   // ← 实际掩码数据作为参数传入

    // 构造函数接收运行时数据
    jit_decompress_specialized_t(int blocks, const uint64_t* bitmask)
        : blocks_(blocks), bitmask_(bitmask) {  // ← 存为成员变量
        generate();   // ← 代码生成在构造函数中发生
    }

    void generate() {
        // 这是一个普通的 C++ 函数。它只执行一次（构造时）。
        // 它读取实际的掩码值并发射机器码。

        int src_off = 0;   // ← C++ 变量，不是寄存器！

        for (int block = 0; block < blocks_; ++block) {
            for (int chunk = 0; chunk < 64; ++chunk) {

                // *** 关键代码 ***
                // bitmask_ 是真实数据 → mask 是已知的 C++ 值
                uint64_t mask = bitmask_[block * 64 + chunk];
                int popcnt = __builtin_popcountll(mask);

                if (mask == 0) {
                    // C++ 看到 mask == 0 → 发射清零存储代码
                    vpxord(zmm0, zmm0, zmm0);
                    // src_off += 0; （不推进，在 JIT 编译时已计算）

                } else if (mask == 0xFFFFFFFFFFFFFFFFULL) {
                    // C++ 看到 mask == 全 1 → 发射直接拷贝代码
                    vmovdqu8(zmm0, ptr[reg_src + src_off]);
                    //                          ^^^^^^^
                    //                          src_off 是 C++ 的 int
                    //                          变成指令中的立即数位移

                } else {
                    // C++ 看到部分掩码 → 将掩码嵌入为立即数
                    mov(rax, mask);
                    //       ^^^^
                    //       发射: 48 B8 01 01 01 01 01 01 01 01
                    //       掩码的值直接编码为机器码的字节！
                    kmovq(k1, rax);
                    vpexpandb(zmm0 | k1 | T_z, ptr[reg_src + src_off]);
                }

                vmovdqu8(ptr[reg_dst + dst_off], zmm0);

                src_off += popcnt;  // ← C++ 加法，不是 asm 指令！
                //                     这个值在运行时已经不存在。
                //                     它在代码生成过程中被消耗掉了。
            }
        }
        ret();
    }
};
```

### 简易示例：常量加法

```cpp
#include "xbyak/xbyak.h"

// 一个 JIT 函数：将一个常量（JIT 编译时已知）加到参数上
class AddConst : public Xbyak::CodeGenerator {
public:
    AddConst(int value) {   // ← 'value' 是运行时数据，但在 JIT 编译时已知
        // Linux ABI：第一个参数在 rdi，返回值在 rax
        lea(rax, ptr[rdi + value]);   // value 变成立即数位移
        //                   ^^^^^
        //                   发射: lea rax, [rdi + 42]
        //                   数字 42 被烧进了机器码的字节中
        ret();
    }
};

int main() {
    int secret = 42;  // 假设这个值来自文件、用户输入等

    AddConst jit(secret);              // JIT 编译：secret → 立即数
    auto f = jit.getCode<int(*)(int)>();
    jit.ready();

    printf("%d\n", f(10));   // 输出 52 — 没有内存加载，没有变量
    printf("%d\n", f(100));  // 输出 142
}
```

生成的机器码：

```asm
lea rax, [rdi + 0x2A]   ; 0x2A = 42，硬编码在指令字节中
ret
```

**没有变量、没有内存加载、没有寄存器保存着 42**。值 42 是指令编码的一部分——就像 `mov rax, 5` 把数字 5 嵌入指令一样。JIT "知道"常量是因为**生成机器码的 C++ 代码可以像普通变量一样读取它，并将它用作立即数操作数**。

### 本质总结

```
普通程序：                            JIT 程序：
  读取数据 → 计算 → 输出结果            读取数据 → 生成代码 → 执行代码 → 输出结果
                                              ↑
                                        数据在这里被"消耗"，
                                        变成了指令的一部分
```

JIT 就是一个**以数据为输入、以机器码为输出的程序**。C++ 的 `if`/`for`/变量赋值在代码生成阶段全部执行完毕，运行时执行的只是生成出来的精简指令序列。这就是为什么 JIT 能做到 AOT 编译器做不到的事——它在生成代码的那一刻，同时拥有程序逻辑和数据的完整信息。

---

## 第四部分：JIT 的分支消除——只生成需要的代码

### 核心理解

JIT 内核**只为特定条件生成对应的那一条分支代码路径，其它分支根本不会被生成**。

这是 JIT 相对于 AOT（提前编译）最本质的优势之一。

### AOT 编译器 vs JIT 编译器的分支处理

#### AOT（传统静态编译）

AOT 编译器在编译时**不知道运行时的数据值**，因此它必须为所有可能的分支都生成代码，并在运行时通过条件判断（`cmp` + `je`/`jne`/`jmp`）来选择执行哪一条路径：

```
; AOT 生成的代码——所有分支都存在
    cmp  [chunk_type], ZERO
    je   .handle_zero          ; 跳转到零块处理
    cmp  [chunk_type], DENSE
    je   .handle_dense         ; 跳转到稠密块处理
    jmp  .handle_partial       ; 否则处理部分块

.handle_zero:
    vpxord zmm0, zmm0, zmm0   ; 零块逻辑
    vmovdqu8 [dst], zmm0
    jmp  .next_chunk

.handle_dense:
    vmovdqu8 zmm0, [src]      ; 稠密块逻辑
    vmovdqu8 [dst], zmm0
    jmp  .next_chunk

.handle_partial:
    kmovq  k1, [mask]         ; 部分块逻辑
    vpexpandb zmm0{k1}{z}, [src]
    vmovdqu8 [dst], zmm0
    jmp  .next_chunk
```

**问题**：即使某个块永远是零块，跳转到 `.handle_dense` 和 `.handle_partial` 的代码仍然存在于二进制文件中，CPU 仍需读取和解码这些无用指令。更重要的是，每个块都需要执行 `cmp`/`je`/`jmp` 分支判断，浪费了时钟周期。

#### JIT（运行时编译）

JIT 在**代码生成阶段**就已经知道每个块的类型（因为位掩码是已知数据）。所以它**只为实际需要的类型生成指令**，不需要的分支代码**根本不存在于生成的机器码中**：

```c++
// C++ 代码生成阶段（JIT 编译时执行）
for (int i = 0; i < num_chunks; i++) {
    if (masks[i] == 0x0) {
        // 该块是零块 → 只生成零块指令
        vpxord(zmm0, zmm0, zmm0);
        vmovdqu8(ptr[dst + i*64], zmm0);
    } else if (masks[i] == 0xFFFFFFFFFFFFFFFF) {
        // 该块是稠密块 → 只生成拷贝指令
        vmovdqu8(zmm0, ptr[src + offset]);
        vmovdqu8(ptr[dst + i*64], zmm0);
    } else {
        // 部分块 → 只生成 expand 指令
        mov(rax, masks[i]);   // 掩码作为立即数
        kmovq(k1, rax);
        vpexpandb(zmm0 | k1 | T_z, ptr[src + offset]);
        vmovdqu8(ptr[dst + i*64], zmm0);
    }
}
```

假设有 4 个块，掩码分别为 `[0x0, 0xFF...FF, 0x0, 0x00FF00FF]`，JIT 生成的机器码**仅包含**：

```
; JIT 生成的代码——扁平化指令流，无分支
; 块 0：零块
    vpxord zmm0, zmm0, zmm0
    vmovdqu8 [dst + 0], zmm0

; 块 1：稠密块
    vmovdqu8 zmm0, [src + 0]
    vmovdqu8 [dst + 64], zmm0

; 块 2：零块
    vpxord zmm0, zmm0, zmm0
    vmovdqu8 [dst + 128], zmm0

; 块 3：部分块
    mov rax, 0x00FF00FF
    kmovq k1, rax
    vpexpandb zmm0{k1}{z}, [src + 64]
    vmovdqu8 [dst + 192], zmm0
```

**关键区别**：
- **没有 `cmp`** — 不需要比较块类型
- **没有 `je`/`jne`/`jmp`** — 不需要条件跳转
- **没有未使用的代码** — 零块不会生成 expand 指令，稠密块不会生成 kmov 指令
- **所有偏移量都是立即数** — `[dst + 0]`、`[src + 64]` 等在生成时就已确定

### 对性能的影响

| 方面 | AOT | JIT |
|------|-----|-----|
| 分支指令 | 每个块 2-3 条 `cmp`/`jmp` | **零条** |
| 分支预测失败惩罚 | 可能发生（~15 周期/次） | **不可能发生** |
| 指令缓存利用 | 所有分支代码占用 I-cache | **只有实际执行的代码占用** |
| 代码膨胀 | 全部分支代码都在二进制中 | **只有需要的指令** |

### 类比

可以这样理解 AOT vs JIT 的分支处理：

- **AOT** 像是印刷一本菜谱书：中餐、西餐、日料的做法全部印在里面，每次做饭时翻到对应的页面（分支跳转）
- **JIT** 像是知道今晚要做什么菜之后，只打印那一道菜的做法——其它菜的做法根本不会出现在纸上
