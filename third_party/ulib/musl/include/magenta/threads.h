#pragma once

#include <magenta/types.h>
#include <threads.h>

#ifdef __cplusplus
extern "C" {
#endif

// Get the mx_handle_t corresponding to the thrd_t. This handle is
// still owned by the C11 thread, and will not persist after the
// thread exits and is joined or detached. Callers must duplicate the
// handle, therefore, if they wish the thread handle to outlive the
// execution of the C11 thread.
mx_handle_t thrd_get_mx_handle(thrd_t t);

#ifdef __cplusplus
}
#endif
