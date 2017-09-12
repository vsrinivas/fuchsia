# zx_thread_write_state

## NAME

thread_write_state - write one aspect of thread state

## SYNOPSIS

```
#include <zircon/syscalls.h>
#include <zircon/syscalls/debug.h>

zx_status_t zx_thread_write_state(
    zx_handle_t handle,
    uint32_t kind,
    void* buffer,
    uint32_t len);
```

## DESCRIPTION

**thread_write_state**() writes one aspect of state of the thread.
Typically this is a register set or "regset".
E.g., integer registers, floating point registers, etc.
Which registers are in which regset is defined by the architecture.
By convention regset 0 contains the general purpose integer registers,
stack pointer, program counter, and ALU flags register if the architecture
defines one. Each register set's contents is defined by a struct in
zircon/syscalls/debug.h.

## RETURN VALUE

**thread_write_state**() returns **ZX_OK** on success.
In the event of failure, a negative error value is returned.

## ERRORS

**ZX_ERR_BAD_HANDLE**  *handle* is not a valid handle.

**ZX_ERR_WRONG_TYPE**  *handle* is not that of a thread.

**ZX_ERR_ACCESS_DENIED**  *handle* lacks *ZX_RIGHT_WRITE*.

**ZX_ERR_INVALID_ARGS**  *kind* is not valid,
or *buffer* is an invalid pointer,
or the provided *len* is too large.

**ZX_ERR_NO_MEMORY**  Temporary out of memory failure.

**ZX_ERR_BAD_STATE**  The thread is not stopped at a point where state
is available. Typically thread state is read by an exception handler
when the thread is stopped due to an exception.

**ZX_ERR_NOT_SUPPORTED**  *kind* is not supported.
This can happen, for example, when trying to write a register set that
is not supported by the h/w the program is currently running on.

**ZX_ERR_UNAVAILABLE**  *kind* is currently unavailable.
This can happen, for example, when a regset requires the chip to be
in a specific mode. The details are dependent on the architecture
and regset.

## SEE ALSO

[thread_read_state](thread_read_state.md).
