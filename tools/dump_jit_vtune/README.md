# dump_jit_vtune

Header-only wrapper around Intel VTune's JIT Profiling API. Include `jit_vtune.h` and call `jit_vtune_register()` to make JIT-compiled code visible in VTune Profiler.

## Files

| File | Description |
|------|-------------|
| `jit_vtune.h` | Header-only API + implementation (`static inline`) |
| `sample.c` | Minimal example: JIT a "return 42" function and register it |

## Build

Requires `ittapi` at `../../3rdparty/ittapi` (already in this repo).

```bash
gcc -O2 -g -I. -I../../3rdparty/ittapi/include -I../../3rdparty/ittapi/src/ittnotify \
    -o sample sample.c ../../3rdparty/ittapi/src/ittnotify/jitprofiling.c -ldl
```

Clean:

```bash
rm -f sample
```

## Run

```bash
# Standalone (without VTune, registration is silently skipped):
./sample

# Under VTune:
vtune -collect hotspots -- ./sample
```

## API

```c
#include "jit_vtune.h"

// After generating JIT code at `code_ptr` with `code_size` bytes:
jit_vtune_register(code_ptr, code_size, "my_jit_kernel");
```

When VTune is **not** attached, `jit_vtune_register` is a no-op (checks `iJIT_IsProfilingActive()`).
