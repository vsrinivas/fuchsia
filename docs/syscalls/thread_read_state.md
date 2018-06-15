# zx_thread_read_state

## NAME

thread_read_state - Read one aspect of thread state.

## SYNOPSIS

```
#include <zircon/syscalls.h>
#include <zircon/syscalls/debug.h>

zx_status_t zx_thread_read_state(
    zx_handle_t handle,
    uint32_t kind,
    void* buffer,
    size_t len);
```

## DESCRIPTION

**thread_read_state**() reads one aspect of state of the thread. The thread
state may only be read when the thread is halted for an exception or the thread
is suspended.

The thread state is highly processor specific. See the structures in
zircon/syscalls/debug.h for the contents of the structures on each platform.

## STATES

### ZX_THREAD_STATE_GENERAL_REGS

The buffer must point to a **zx_thread_state_general_regs_t** structure that
contains the general registers for the current architecture.

### ZX_THREAD_STATE_FP_REGS

The buffer must point to a **zx_thread_state_fp_regs_t** structure. On 64-bit
ARM platforms, float point state is in the vector registers and this structure
is empty.

### ZX_THREAD_STATE_VECTOR_REGS

The buffer must point to a **zx_thread_state_vector_regs_t** structure.

### ZX_THREAD_STATE_SINGLE_STEP

The buffer must point to a **zx_thread_state_single_step_t** value which
may contain either 0 (normal running), or 1 (single stepping enabled).

### ZX_THREAD_X86_REGISTER_FS

The buffer must point to a **zx_thread_x86_register_fs_t** structure which contains
a uint64. This is only relevant on x86 platforms.

### ZX_THREAD_X86_REGISTER_GS

The buffer must point to a **zx_thread_x86_register_gs_t** structure which contains
a uint64. This is only relevant on x86 platforms.

## RETURN VALUE

**thread_read_state**() returns **ZX_OK** on success.
In the event of failure, a negative error value is returned.

## ERRORS

**ZX_ERR_BAD_HANDLE**  *handle* is not a valid handle.

**ZX_ERR_WRONG_TYPE**  *handle* is not that of a thread.

**ZX_ERR_ACCESS_DENIED**  *handle* lacks *ZX_RIGHT_READ*.

**ZX_ERR_INVALID_ARGS**  *kind* is not valid or *buffer* is an invalid pointer.

**ZX_ERR_NO_MEMORY**  Temporary out of memory failure.

**ZX_ERR_BUFFER_TOO_SMALL**  The buffer length *len* is too small to hold
the data required by *kind*.

**ZX_ERR_BAD_STATE**  The thread is not stopped at a point where state
is available. The thread state may only be read when the thread is stopped due
to an exception.

**ZX_ERR_NOT_SUPPORTED**  *kind* is not supported.
This can happen, for example, when trying to read a register set that
is not supported by the hardware the program is currently running on.

## SEE ALSO

[thread_write_state](thread_write_state.md).
