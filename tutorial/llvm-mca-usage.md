# llvm-mca 瓶颈分析方法论

## 1. 基本用法

```bash
# 从 C/C++ 源码生成汇编并分析
gcc -O3 -march=native -S -o kernel.s kernel.c
llvm-mca -mcpu=icelake-server kernel.s

# 常用分析选项
llvm-mca -mcpu=icelake-server \
  -timeline           \  # 指令时间线
  -bottleneck-analysis \  # 瓶颈分析
  -resource-pressure   \  # 资源压力
  -iterations=300      \  # 模拟迭代次数
  kernel.s

# 只分析特定代码块 (用注释标记)
# 汇编中加入:
#   # LLVM-MCA-BEGIN my_kernel
#   ...
#   # LLVM-MCA-END my_kernel
```

---

## 2. 输出解读与瓶颈判断

### 2.1 Summary 输出

```
Iterations:        100
Instructions:      800
Total Cycles:      425
Total uOps:        900

Dispatch Width:    6
uOps Per Cycle:    2.12
IPC:               1.88
Block RThroughput: 4.0
```

| 指标 | 含义 | 判断依据 |
|------|------|----------|
| **IPC** | 每周期指令数 | < Dispatch Width × 0.5 → 有瓶颈 |
| **uOps Per Cycle** | 每周期微操作数 | 远低于 Dispatch Width → 瓶颈 |
| **Block RThroughput** | 代码块最小吞吐周期 | 实际 cycles >> RThroughput → 有浪费 |
| **Dispatch Width** | 前端最大发射宽度 | 参考上界 |

### 2.2 Front-end Bound 判断

**核心指标：** 前端无法以足够速率发射 µops

```
=== Dispatch Statistics ===
[0] - 25 (25.0%)     ← 0 个 µop 被发射的周期数
[1] - 15 (15.0%)
[2] - 20 (20.0%)
[3] - 10 (10.0%)
[4] - 10 (10.0%)
[5] - 10 (10.0%)
[6] - 10 (10.0%)     ← 满发射
```

| 判断条件 | 说明 |
|----------|------|
| `[0]` 占比 **> 20%** | 大量周期没有任何 µop 发射 → **前端饥饿** |
| 低档位 `[0]-[2]` 占比总和 **> 50%** | 前端持续供给不足 |
| `uOps Per Cycle` 远低于 `Dispatch Width` | 前端成为瓶颈 |

### 2.3 Back-end Bound 判断

**核心指标：** Resource Pressure (资源压力) 和 Timeline

```
=== Resource pressure per iteration ===
[0]  [1]  [2]  [3]  [4]  [5]  [6]  [7]
2.00 3.50 1.00 1.00 0.50 3.50 0.50 0.50
```

| 判断条件 | 说明 |
|----------|------|
| 某端口压力 **≈ Block RThroughput** | 该端口是**吞吐瓶颈** |
| 某端口压力 **远大于**其他端口 | 端口不均衡 → **Core Bound** |
| Timeline 中 `D` → `e` 间隔长 | Dispatch 后等待 → 操作数未就绪 → **依赖链** |
| Timeline 中 `d` 小写 | µop 被 dispatch 但等待资源 → **Back-end 压力** |

#### Timeline 详解

```bash
llvm-mca -timeline -mcpu=icelake-server kernel.s
```

```
Timeline view:
     0123456789
[0,0] DeeeER..    vmulps  xmm0, xmm1, xmm2
[0,1] D==eeeER.   vaddps  xmm3, xmm0, xmm4    ← "==" 等待操作数
[0,2] .D=eeeER.   vmulps  xmm5, xmm3, xmm6    ← 依赖链延续
```

| 符号 | 含义 |
|------|------|
| `D` | Dispatched (大写: 立即发射) |
| `d` | Dispatched (小写: 等待后才发射 → back-end 压力) |
| `e` | Executing |
| `E` | Executed (完成) |
| `R` | Retired |
| `=` | 等待操作数 → **依赖链导致的延迟** |
| `-` | 等待资源 → **端口竞争** |

---

## 3. Front-end Bound 原因与解决

### 原因

| 原因 | llvm-mca 中的表现 |
|------|-------------------|
| **指令解码复杂** | 单条指令分裂成多 µops, `Total uOps >> Instructions` |
| **µop Cache 压力** | 代码块太大超出 DSB 容量 |
| **长编码指令** | x86 变长编码, 解码带宽受限 |
| **跨缓存行指令** | 取指效率降低 |

### 解决方案

```asm
# 1. 避免复杂指令 → 拆为简单 µop
# Bad: gather 指令 ≈ 多个 µops
vgatherdps ymm0, [rsi + ymm1*4], ymm2   # 可能 >8 µops

# Good: 手动标量加载 (如果 gather 不划算)
vmovss xmm0, [rsi + r8*4]
vinsertps xmm0, xmm0, [rsi + r9*4], 0x10

# 2. 减少代码体积
# Bad: 大量展开
.rept 32
  vmovaps ymm0, [rdi]
  add rdi, 32
.endr

# Good: 适度展开 + 循环
mov ecx, 8
.loop:
  vmovaps ymm0, [rdi]
  vmovaps ymm1, [rdi+32]
  vmovaps ymm2, [rdi+64]
  vmovaps ymm3, [rdi+96]
  add rdi, 128
  dec ecx
  jnz .loop

# 3. 对齐循环入口
.p2align 5   # 32 字节对齐
.loop:
  ...
```

| 方法 | 何时使用 |
|------|----------|
| 避免高 µop 指令 | `uOps >> Instructions` |
| 控制展开程度 | 代码块大，`[0]` dispatch 空闲多 |
| 循环对齐 | 循环密集型代码 |
| 使用更简单的指令序列 | 复杂指令被拆成大量µops |

---

## 4. Back-end Bound 原因与解决

### 4.1 Core Bound (端口竞争)

#### 诊断

```
Resource pressure per iteration:
[0]    [1]    [2]    [3]    [4]    [5]    [6]    [7]
1.00   4.00   1.50   1.50   1.00   1.00   1.00   1.00
         ^
         端口 1 是瓶颈 (压力 = Block RThroughput)
```

**判断规则:** 端口压力最大值 ≈ Block RThroughput → 该端口是吞吐瓶颈

#### 原因与解决

| 原因 | 解决方案 |
|------|----------|
| **FMA 全在同一端口** | 交替使用不同端口的指令 |
| **Shuffle 密集** | 减少 permute/shuffle, 用 blend 替代 |
| **DIV 指令** | 用乘法近似, 查表, 或 Newton-Raphson |
| **单端口独占指令** | 重排指令序列, 减少该类指令 |

```asm
# 常见端口映射 (Ice Lake)
# Port 0: FMA, MUL, DIV
# Port 1: FMA, ADD, MUL
# Port 2/3: LOAD
# Port 4/9: STORE_DATA
# Port 5: ADD, SHUFFLE, PERMUTE
# Port 6: ADD, BRANCH
# Port 7/8: STORE_AGU

# Bad: 全是 shuffle → 端口 5 瓶颈
vpermps  ymm0, ymm1, ymm2    # port 5
vshufps  ymm3, ymm4, ymm5, 0 # port 5
vpermps  ymm6, ymm7, ymm8    # port 5

# Good: 混合不同端口指令
vpermps  ymm0, ymm1, ymm2    # port 5
vfmadd231ps ymm3, ymm4, ymm5 # port 0/1
vpermps  ymm6, ymm7, ymm8    # port 5
vaddps   ymm9, ymm10, ymm11  # port 0/1/5
```

### 4.2 Dependency Chain (依赖瓶颈)

#### 诊断

Timeline 中大量 `=` (等待操作数):

```
[0,0] DeeeER..    vfmadd231ps ymm0, ymm1, ymm2    # 4 cycles 延迟
[0,1] D===eeeER.  vfmadd231ps ymm0, ymm0, ymm3    # 等 3 cycle → 依赖 ymm0
[0,2] .D====eeeER vfmadd231ps ymm0, ymm0, ymm4    # 继续等
```

**判断:** `实际 cycles / iteration` >> `Block RThroughput` 且 timeline 充满 `=`

#### 原因与解决

```asm
# Bad: 单一累加器 → 受 FMA 延迟 (4 cycles) 限制
# IPC ≈ 0.25 (每 4 cycle 才能执行 1 条)
vfmadd231ps ymm0, ymm1, [rdi]
vfmadd231ps ymm0, ymm2, [rdi+32]
vfmadd231ps ymm0, ymm3, [rdi+64]
vfmadd231ps ymm0, ymm4, [rdi+96]

# Good: 多累加器 → FMA 延迟被隐藏
# 4 个独立链 × 4 cycle 延迟 → 每 cycle 1 条 FMA
vfmadd231ps ymm0, ymm4, [rdi]       # chain 0
vfmadd231ps ymm1, ymm5, [rdi+32]    # chain 1
vfmadd231ps ymm2, ymm6, [rdi+64]    # chain 2
vfmadd231ps ymm3, ymm7, [rdi+96]    # chain 3

# 最后合并
vaddps ymm0, ymm0, ymm1
vaddps ymm2, ymm2, ymm3
vaddps ymm0, ymm0, ymm2
```

**所需独立链数 = 操作延迟 × 可用端口数**

| 操作 | 延迟 | FMA Ports | 需要独立链数 |
|------|------|-----------|-------------|
| FMA (Skylake+) | 4 | 2 | **8** |
| FMA (Zen 2/3) | 5 | 2 | **10** |
| ADD | 3-4 | 2 | 6-8 |
| MUL | 4-5 | 2 | 8-10 |

### 4.3 Memory Bound (Load/Store 瓶颈)

#### 诊断

```
Resource pressure per iteration:
[0]    [1]    [2]    [3]    [4]    [5]    [6]    [7]
1.00   1.00   4.00   4.00   2.00   1.00   0.00   2.00
                ^      ^
              Load 端口是瓶颈
```

#### 解决

```asm
# Bad: 过多独立 load
vmovaps ymm0, [rdi]
vmovaps ymm1, [rdi+32]
vmovaps ymm2, [rdi+64]
vmovaps ymm3, [rdi+96]
vmovaps ymm4, [rdi+128]   # 超出 2 load ports/cycle

# Good: 使用 memory operand 与 FMA 融合 (不多占 load port)
vfmadd231ps ymm0, ymm4, [rdi]       # load 融合进 FMA
vfmadd231ps ymm1, ymm5, [rdi+32]

# Good: 交错 load 和计算
vmovaps ymm0, [rdi]
vfmadd231ps ymm2, ymm3, ymm4   # 计算
vmovaps ymm1, [rdi+32]
vfmadd231ps ymm5, ymm6, ymm7   # 计算
```

---

## 5. Bottleneck Analysis 输出解读

```bash
llvm-mca -bottleneck-analysis -mcpu=icelake-server kernel.s
```

输出形如:

```
Bottleneck Analysis:
  The throughput is limited by the Port 1 resource.
  Percentage of cycles with backend pressure:
    Port 0: 25.0%
    Port 1: 95.0%  ← 瓶颈端口
    Port 5: 30.0%
```

| 输出 | 含义 | 行动 |
|------|------|------|
| `limited by Port X` | 端口 X 是吞吐瓶颈 | 减少该端口指令 |
| `limited by data dependencies` | 依赖链限制 | 增加 ILP |
| `limited by dispatch rate` | 前端发射受限 | 减少 µops / 对齐 |

---

## 6. 诊断流程

```
llvm-mca -timeline -bottleneck-analysis -resource-pressure kernel.s
                        |
         +--------------+--------------+
         |              |              |
  IPC 低且 [0] 多   Port X 饱和    Timeline 多 "="
         |              |              |
   [Front-end]     [Core Bound]   [Dependency]
         |              |              |
  检查 µop 数     混合指令类型    多累加器/重排
  减少代码体积    替换高开销指令   打破依赖链
  对齐循环入口    指令调度
```

## 7. 快速参考

| 瓶颈类型 | llvm-mca 关键信号 | 解决方案 |
|----------|-------------------|----------|
| Front-end | `[0]` dispatch > 20%, uOps >> Instructions | 减少 µops, 对齐, 控制展开 |
| Port 竞争 | 某端口压力 ≈ RThroughput | 替换/混合指令 |
| 依赖链 | Timeline 大量 `=`, cycles >> RThroughput | 多累加器: latency × ports |
| Load 瓶颈 | Port 2/3 饱和 | Memory operand 融合, 交错 |
| Store 瓶颈 | Port 4/7/8/9 饱和 | 减少 store, 合并写入 |

```bash
# 一键完整分析
llvm-mca -mcpu=native -timeline -bottleneck-analysis \
  -resource-pressure -iterations=300 kernel.s
```
