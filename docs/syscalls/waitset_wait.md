# mx_waitset_wait

## NAME

waitset_wait - wait on (the entries of) a wait set

## SYNOPSIS

```
#include <magenta/syscalls.h>

mx_status_t mx_waitset_wait(mx_handle_t waitset_handle,
                             mx_time_t deadline,
                             mx_waitset_result_t* results,
                             uint32_t* count);
```

## DESCRIPTION

**waitset_wait**() waits until one of its entries has a result to report
(waited for bits are pending or the handle was closed) or the specified
*deadline* has passed.

*waitset_handle* must have the **MX_RIGHT_READ** right.

*count* is a required in-out parameter: its input value is the maximum
number of results to write to the *results* buffer, while its output value is
the number that was actually written. (*results* may be null only if the input
value of *count* is zero.)

Each result (entry in the *results* buffer) is an **mx_waitset_result_t**:
```
typedef struct mx_waitset_result {
    uint64_t cookie;
    mx_status_t status;
    mx_signals_t observed;
} mx_waitset_result_t;
```
*cookie* is set to the cookie for the entry with a result to report (as provided
to **waitset_add**()); each entry yields at most one result. *status* is
**NO_ERROR** if the watched signals provided to **waitset_add**() were
satisfied, **ERR_BAD_STATE** if the watched signals became unsatisfiable, or
**ERR_CANCELED** if the entry's handle was closed. **observed** is set
to the entry's handle's observed signals at some point shortly before
**waitset_wait**() returned.

## RETURN VALUE

**waitset_wait**() returns **NO_ERROR** if there was a result
to report from one of the wait set's entries before the deadline passed. Only on
success are *count* and *results* written to.

It is possible for *count* to be 0 on success if the condition that woke the
waiter becomes untrue during a very small window.

On failure, an error value is returned. In particular, **ERR_TIMED_OUT** is
returned if *deadline* passes with no results to report.

## ERRORS

**ERR_NO_MEMORY**  (Temporary) Failure due to lack of memory.

**ERR_BAD_HANDLE**  *waitset_handle* is not a valid handle.

**ERR_CANCELED** The *waitset_handle* was closed while waiting on it.

**ERR_WRONG_TYPE**  *waitset_handle* is not a handle to a wait set.

**ERR_INVALID_ARGS**  *count* or *results* are invalid pointers.

**ERR_ACCESS_DENIED**  *waitset_handle* does not have the **MX_RIGHT_READ**
right.

**ERR_TIMED_OUT**  The specified deadline passed before any results were
available.

## SEE ALSO

[waitset_create](waitset_create.md),
[waitset_add](waitset_remove.md),
[waitset_remove](waitset_remove.md),
[handle_close](handle_close.md).
