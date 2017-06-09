# mx_thread_read_state

## NAME

thread_read_state - read one aspect of thread state

## SYNOPSIS

```
#include <magenta/syscalls.h>
#include <magenta/syscalls/debug.h>

mx_status_t mx_thread_read_state(
    mx_handle_t handle,
    uint32_t kind,
    void* buffer,
    uint32_t len,
    uint32_t actual[1]);
```

## DESCRIPTION

**thread_read_state**() reads one aspect of state of the thread.
Typically this is a register set or "regset".
E.g., integer registers, floating point registers, etc.
Which registers are in which regset is defined by the architecture.
By convention regset 0 contains the general purpose integer registers,
stack pointer, program counter, and ALU flags register if the architecture
defines one. Each register set's contents is defined by a struct in
magenta/syscalls/debug.h.

## RETURN VALUE

**thread_read_state**() returns **MX_OK** on success.
In the event of failure, a negative error value is returned.

## ERRORS

**MX_ERR_BAD_HANDLE**  *handle* is not a valid handle.

**MX_ERR_WRONG_TYPE**  *handle* is not that of a thread.

**MX_ERR_ACCESS_DENIED**  *handle* lacks *MX_RIGHT_READ*.

**MX_ERR_INVALID_ARGS**  *kind* is not valid,
or *buffer* or *actual* are invalid pointers,
or the provided *buffer_len* is too large.

**MX_ERR_NO_MEMORY**  Temporary out of memory failure.

**MX_ERR_BUFFER_TOO_SMALL**  The provided buffer is too small to hold *kind*.
The required size is returned in *actual[0]*.

**MX_ERR_BAD_STATE**  The thread is not stopped at a point where state
is available. Typically thread state is read by an exception handler
when the thread is stopped due to an exception.

**MX_ERR_NOT_SUPPORTED**  *kind* is not supported.
This can happen, for example, when trying to read a register set that
is not supported by the h/w the program is currently running on.

**MX_ERR_UNAVAILABLE**  *kind* is currently unavailable.
This can happen, for example, when a regset requires the chip to be
in a specific mode. The details are dependent on the architecture
and regset.

## SEE ALSO

[thread_write_state](thread_write_state.md).
