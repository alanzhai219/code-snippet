#ifndef JIT_VTUNE_H
#define JIT_VTUNE_H

#include <stddef.h>
#include "jitprofiling.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Register JIT-compiled code with VTune Profiler.
 * No-op when VTune is not attached.
 */
static inline void jit_vtune_register(const void *code, size_t code_size,
                                      const char *code_name) {
    if (iJIT_IsProfilingActive() != iJIT_SAMPLING_ON)
        return;

    iJIT_Method_Load jmethod = {0};
    jmethod.method_id = iJIT_GetNewMethodID();
    jmethod.method_name = (char *)code_name;
    jmethod.class_file_name = NULL;
    jmethod.source_file_name = NULL;
    jmethod.method_load_address = (void *)code;
    jmethod.method_size = (unsigned int)code_size;

    iJIT_NotifyEvent(iJVM_EVENT_TYPE_METHOD_LOAD_FINISHED, (void *)&jmethod);
}

#ifdef __cplusplus
}
#endif

#endif /* JIT_VTUNE_H */
