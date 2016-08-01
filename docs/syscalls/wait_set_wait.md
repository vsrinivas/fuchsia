# mx_wait_set_wait

## NAME

wait_set_wait - wait on (the entries of) a wait set

## SYNOPSIS

```
#include <magenta/syscalls.h>

mx_status_t mx_wait_set_wait(mx_handle_t wait_set_handle,
                             mx_time_t timeout,
                             uint32_t* num_results,
                             mx_wait_set_result_t* results,
                             uint32_t* max_results);
```

## DESCRIPTION

**wait_set_wait**() waits until one of its entries has a result to report (is
satisfied, has become unsatisfiable, or was "cancelled") or the specified
*timeout* has elapsed.

*wait_set_handle* must have the **MX_RIGHT_READ** right.

*num_results* is a required in-out parameter: its input value is the maximum
number of results to write to the *results* buffer, while its output value is
the number that was actually written. (*results* may be null only if the input
value of *num_results* is zero.)

Each result (entry in the *results* buffer) is an **mx_wait_set_result_t**:
```
typedef struct mx_wait_set_result {
    uint64_t cookie;
    mx_status_t wait_result;
    uint32_t reserved;
    mx_signals_state_t signals_state;
} mx_wait_set_result_t;
```
*cookie* is set to the cookie for the entry with a result to report (as provided
to **wait_set_add**()); each entry yields at most one result. *wait_result* is
**NO_ERROR** if the watched signals provided to **wait_set_add**() were
satisfied, **ERR_BAD_STATE** if the watched signals became unsatisfiable, or
**ERR_CANCELLED** if the entry's handle was closed. **signals_state** is set to
the state of the entry's handle's signals at some point shortly before
**wait_set_wait**() returned. **reserved** is set to zero.

*max_results* is an optional out parameter: its output value is the maximum
number of results that could have been reported; this is mainly of interest if
it is larger than the input value of *num_results*.

## RETURN VALUE

**wait_set_wait**() returns **NO_ERROR** (which is zero) if there was a result
to report from one of the wait set's entries before the timeout elapsed. Only on
success are *num_results*, *results*, and (optionally) *max_results* written to.

On failure, a (strictly) negative error value is returned. In particular,
**ERR_TIMED_OUT** is returned if *timeout* elapses with no results to report.

## ERRORS

**ERR_NO_MEMORY**  (Temporary) Failure due to lack of memory.

**ERR_BAD_HANDLE**  *wait_set_handle* is not a valid handle.

**ERR_INVALID_ARGS**  *wait_set_handle* is not a handle to a wait set,
*num_results* is not valid, *results* is not valid, or *max_results* is not
valid.

**ERR_ACCESS_DENIED**  *wait_set_handle* does not have the **MX_RIGHT_READ**
right.

**ERR_TIMED_OUT**  The specified timeout elapsed before any results were
available.

## SEE ALSO

[wait_set_create](wait_set_create.md),
[wait_set_add](wait_set_remove.md),
[wait_set_remove](wait_set_remove.md),
[handle_close](handle_close.md).
