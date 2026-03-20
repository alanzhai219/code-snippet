# dump_jit_perf

Profile JIT code with Linux perf. Header-only, no dependencies.

## Quick Start

### 1. Register JIT Code

```cpp
#include "dump_jit_perf.h"

// After generating JIT code:
jit_perf_dump::register_jit_code_linux_perf(code_ptr, code_size, "my_kernel");
```

### 2. Profile

```bash
# Build
g++ -O2 -std=c++11 -I../../3rdparty/xbyak -o example example.cpp

# Record samples
perf record -k 1 ./example 5

# Inject JIT symbols (creates .so files with debug info)
perf inject --jit -i perf.data -o perf.jit.data

# View report
perf report -i perf.jit.data

# View assembly hotspots
perf annotate -i perf.jit.data -s jit_sum_array --stdio
```

## Command Reference

| Command | What it does |
|---------|--------------|
| `perf record -k 1 ./app` | Sample CPU events, `-k 1` syncs timestamps with jitdump |
| `perf inject --jit -i A -o B` | Convert jitdump to ELF .so (required for annotate) |
| `perf report -i file` | Show which functions are hot |
| `perf annotate -s sym` | Show assembly with per-instruction hotspots |
| `perf stat -e events ./app` | Count hardware events (no sampling) |
| `perf stat --topdown ./app` | Show frontend/backend bottleneck breakdown |

## Common Options

| Option | Meaning |
|--------|---------|
| `-i file` | Input file |
| `-o file` | Output file |
| `-s symbol` | Specify symbol to annotate |
| `--stdio` | Text output (vs interactive TUI) |
| `-e events` | Specify events: `cycles`, `cache-misses`, `branch-misses`, etc. |
| `-g` | Record call graph (stack traces) |

## Hardware Events

```bash
# Cache misses
perf stat -e L1-dcache-load-misses,LLC-load-misses ./example 5

# TopDown analysis
perf stat --topdown ./example 5

# Sample multiple events
perf record -e '{cycles,cache-misses}' -k 1 ./example 5
perf inject --jit -i perf.data -o perf.jit.data

# View annotated assembly with event counts
# you can view the several parts with different events by changing the symbol name
perf annotate -i perf.jit.data -s jit_sum_array --stdio
```

## Output Files

| File | Location |
|------|----------|
| jitdump | `$HOME/.debug/jit/jitperf.*/jit-<pid>.dump` |
| perfmap | `/tmp/perf-<pid>.map` |
| injected ELF | `$HOME/.debug/jit/jitperf.*/jitted-<pid>-*.so` |

## Install perf

```bash
# Ubuntu/Debian
sudo apt install linux-tools-generic

# Fedora
sudo dnf install perf
```

## Kernel Permissions

If perf shows permission errors or missing symbols:

```bash
# Allow perf to access performance counters
sudo bash -c "echo 0 > /proc/sys/kernel/perf_event_paranoid"

# Allow reading kernel symbols
sudo bash -c "echo 0 > /proc/sys/kernel/kptr_restrict"

# Allow ptrace (for some perf features)
sudo bash -c "echo 0 > /proc/sys/kernel/yama/ptrace_scope"
```

To make permanent, add to `/etc/sysctl.conf`:
```
kernel.perf_event_paranoid = 0
kernel.kptr_restrict = 0
kernel.yama.ptrace_scope = 0
```
