# Static Analysis

## compile with -O2
```bash
g++ -O2 -o vec_add vec_add.cpp
```

## dump asm code

1. with compiler:
```bash
clang++ -O2 -S -o vec_add.s vec_add.cpp
```

2. with objdump:
```bash
# parse symbols
nm vec_add | grep vec_sum
00000000004011d0 T _Z7vec_sumPfi

## disassemble the function
objdump -d --disassemble=_Z7vec_sumPfi vec_add > vec_sum.asm
```

## extract hot asm code snippets (experimental)

| note: this is a very rough approach. `for` loop is always hot.

```assembly
	vaddss	(%rdi,%rdx,4), %xmm0, %xmm0
	vaddss	4(%rdi,%rdx,4), %xmm0, %xmm0
	vaddss	8(%rdi,%rdx,4), %xmm0, %xmm0
	vaddss	12(%rdi,%rdx,4), %xmm0, %xmm0
	vaddss	16(%rdi,%rdx,4), %xmm0, %xmm0
	vaddss	20(%rdi,%rdx,4), %xmm0, %xmm0
	vaddss	24(%rdi,%rdx,4), %xmm0, %xmm0
	vaddss	28(%rdi,%rdx,4), %xmm0, %xmm0
```

## TODO