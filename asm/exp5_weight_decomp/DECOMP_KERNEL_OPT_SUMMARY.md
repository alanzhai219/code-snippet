# Decomp Kernel Optimization Summary

## Scope
This summary captures the fixes and optimization packaging for the decompression kernels in `exp5_weight_decomp`.

## What was done

1. Created new optimized kernel headers (without modifying API in old headers):
   - `decomp_kernel_ref_opt.hpp`
   - `decomp_kernel_avx512_opt.hpp`
   - `decomp_kernel_jit_opt.hpp`

2. Integrated opt kernels into benchmark:
   - `decomp_bench.cpp` now includes `_opt` headers and calls `_opt` symbols.

3. Built and ran the benchmark successfully with:
   - `g++ decomp_bench.cpp -I../../3rdparty/xbyak -march=native -O2 -o decomp_bench_opt`
   - `./decomp_bench_opt`

## Fixed issues carried into `_opt` kernels

- Correct sparse source data generation in benchmark (pass-by-reference).
- Correct JIT output buffer verification in benchmark.
- Correct compressed stream alignment logic:
  - aligned by compressed-stream offset, not by absolute pointer address.
- JIT fixes:
  - correct bitmask addressing,
  - avoid too-large label jump issue (`T_NEAR`),
  - resolve register conflicts,
  - use memory-form `vpexpandb`,
  - unroll per block to reduce inner loop overhead.

## Current performance result

### Single run (`decomp_bench_opt`)
- AVX-512 (4x Unroll): `0.328 ms`
- JIT-AVX-512 (4x Unroll, JIT): `0.350 ms`

### 10-run average
- AVX-512 (4x Unroll): `0.2807 ms`
- JIT-AVX-512 (4x Unroll, JIT): `0.2983 ms`
- JIT vs AVX gap: `+6.27%` (JIT slower)

## Conclusion

- The optimized JIT kernel is correct and significantly improved from the initial version.
- On this platform, intrinsic AVX-512 4x unroll remains the best-performing path.
