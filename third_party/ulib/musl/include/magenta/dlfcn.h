#pragma once

#include <dlfcn.h>
#include <magenta/types.h>

#ifdef __cplusplus
extern "C" {
#endif

void* dlopen_vmo(mx_handle_t, int);

// Replace the handle to the "loader service" used to map names
// to VM objects for dlopen et al.  This takes ownership of the
// given handle, and gives the caller ownership of the old handle
// in the return value.
mx_handle_t dl_set_loader_service(mx_handle_t new_svc);

#ifdef __cplusplus
}
#endif
