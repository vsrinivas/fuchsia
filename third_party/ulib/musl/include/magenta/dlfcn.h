#pragma once

#include <dlfcn.h>
#include <magenta/types.h>

#ifdef __cplusplus
extern "C" {
#endif

void* dlopen_vmo(mx_handle_t, int);

#ifdef __cplusplus
}
#endif
