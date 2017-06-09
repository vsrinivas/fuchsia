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

// Converts a threads.h-style status value to an |mx_status_t|.
static inline mx_status_t __PURE thrd_status_to_mx_status(int thrd_status) {
    switch (thrd_status) {
    case thrd_success:
        return MX_OK;
    case thrd_nomem:
        return MX_ERR_NO_MEMORY;
    case thrd_timedout:
        return MX_ERR_TIMED_OUT;
    case thrd_busy:
        return MX_ERR_SHOULD_WAIT;
    default:
    case thrd_error:
        return MX_ERR_INTERNAL;
    }
}

#ifdef __cplusplus
}
#endif
