# CPU Performance Bottleneck Analysis with perf

## 1. Top-Down Analysis Model

```
                    Retiring (useful work)
                   /
Pipeline Slots ----
                   \
                    Not Retiring ---- Front-end Bound
                                 \
                                  Back-end Bound
                                      |
                              +-------+-------+
                              |               |
                         Memory Bound    Core Bound
```

## 2. Key Metrics & Thresholds

### Quick Diagnosis Commands

```bash
# Top-Down Level 1 & 2
perf stat -M TopdownL1,TopdownL2 -d -d ./program

# Specific metrics
perf stat -e cycles,instructions,\
topdown-retiring,topdown-bad-spec,\
topdown-fe-bound,topdown-be-bound ./program
```

### Metric Thresholds

| Metric | Meaning | Bottleneck If |
|--------|---------|---------------|
| `topdown-fe-bound` | Front-end can't supply µops | **> 20%** |
| `topdown-be-bound` | Back-end can't consume µops | **> 30%** |
| `topdown-retiring` | Useful µops executed | **< 50%** (inefficient) |
| `topdown-bad-spec` | Mispredicted µops | Branch prediction issue |

---

## 3. Front-end Bound

### Sub-metrics

| Metric | Event | Threshold |
|--------|-------|-----------|
| ICache Miss | `L1-icache-load-misses` | > 5% |
| ITLB Miss | `iTLB-load-misses` | > 1% |
| Branch Miss | `branch-misses` | > 2% |
| DSB Miss | `idq.dsb_uops / idq.all_uops` | < 70% |

### Causes & Solutions

| Cause | Solution |
|-------|----------|
| **ICache Miss** | PGO (`-fprofile-use`), `__attribute__((hot/cold))` |
| **Branch Mispredict** | `__builtin_expect()`, branchless code |
| **Large Code** | `-Os`, reduce inlining, code splitting |
| **ITLB Miss** | Reduce code pages, use huge pages |

```cpp
// Branch hint
if (__builtin_expect(rare_condition, 0)) { ... }

// Hot/cold separation
__attribute__((hot)) void frequent_func() { }
__attribute__((cold)) void error_handler() { }
```

---

## 4. Back-end Bound

### 4.1 Memory Bound

#### Diagnosis

```bash
perf stat -e L1-dcache-load-misses,L1-dcache-loads,\
LLC-load-misses,LLC-loads ./program
```

| Level | Miss Rate Threshold | Latency |
|-------|---------------------|---------|
| L1D | > 5% | 4-5 cycles |
| L2 | > 10% | 12-15 cycles |
| L3 | > 20% | 40-50 cycles |
| DRAM | - | 100+ cycles |

#### Causes & Solutions

| Cause | Solution |
|-------|----------|
| **Poor Locality** | Blocking/Tiling |
| **Strided Access** | AoS → SoA conversion |
| **Cache Conflict** | Padding to avoid 2^n stride |
| **No Prefetch** | `__builtin_prefetch()` |
| **False Sharing** | `alignas(64)` |
| **TLB Miss** | Huge pages |

```cpp
// Blocking for cache
for (int i0 = 0; i0 < N; i0 += BLOCK)
for (int j0 = 0; j0 < N; j0 += BLOCK)
for (int k0 = 0; k0 < N; k0 += BLOCK)
  // process block

// Prefetch
__builtin_prefetch(&data[i + 16], 0, 3);
```

### 4.2 Core Bound

#### Diagnosis

```bash
perf stat -e uops_dispatched_port.port_0,\
uops_dispatched_port.port_1,\
uops_dispatched_port.port_5 ./program
```

#### Causes & Solutions

| Cause | Solution |
|-------|----------|
| **Port Contention** | Mix instruction types |
| **Long Dependency Chain** | Multiple accumulators (ILP) |
| **Division Heavy** | Multiply by reciprocal |
| **Scalar Code** | Vectorization (SIMD) |

```cpp
// Break dependency chain with multiple accumulators
// Before
float sum = 0;
for (int i = 0; i < n; i++) sum += a[i];

// After: 4-way parallel
float s0=0, s1=0, s2=0, s3=0;
for (int i = 0; i < n; i += 4) {
    s0 += a[i]; s1 += a[i+1]; s2 += a[i+2]; s3 += a[i+3];
}
float sum = s0 + s1 + s2 + s3;
```

---

## 5. Quick Reference

| Bottleneck | Key Metric | First Action |
|------------|------------|--------------|
| ICache | `L1-icache-load-misses` | PGO |
| Branch | `branch-misses > 2%` | `__builtin_expect` |
| L1D | `L1-dcache-load-misses` | Blocking |
| L2/L3 | `LLC-load-misses` | Prefetch |
| DRAM | BW saturated | Reduce data |
| Port | Single port > 80% | Instruction mix |
| Dependency | IPC < 1 | Multi-accumulator |

---

## 6. Diagnosis Flowchart

```
Start → perf stat -M TopdownL1
              |
    +---------+---------+
    |         |         |
FE>20%    BE>30%    Retiring<50%
    |         |         |
[Front-end]  [Back-end] [Algorithm]
    |         |
 ICache?   Memory?──→ Cache miss? → Blocking/Prefetch
 Branch?      |
           Core?──→ Port saturation? → ILP/Vectorize
```

## 7. Useful Commands

```bash
# Full analysis
perf stat -M TopdownL1,TopdownL2 -d -d ./program

# Record for flamegraph
perf record -g ./program && perf report

# Memory access analysis
perf mem record ./program && perf mem report

# Cache analysis
perf stat -e cache-references,cache-misses ./program

# Branch analysis
perf stat -e branches,branch-misses ./program
```
