# zx_process_start

## NAME

process_start - start execution on a process

## SYNOPSIS

```
#include <zircon/syscalls.h>

zx_status_t zx_process_start(zx_handle_t handle, zx_handle_t thread,
                             zx_vaddr_t entry, zx_vaddr_t stack,
                             zx_handle_t arg1, uintptr_t arg2);
```

## DESCRIPTION

**process_start**() is similar to **thread_start**(), but is used for the
purpose of starting the first thread in a process.

**process_start**() causes a thread to begin execution at the program
counter specified by *entry* and with the stack pointer set to *stack*.
The arguments *arg1* and *arg2* are arranged to be in the architecture
specific registers used for the first two arguments of a function call
before the thread is started.  All other registers are zero upon start.

The first argument (*arg1*) is a handle, which will be transferred from
the process of the caller to the process which is being started, and an
appropriate handle value will be placed in arg1 for the newly started
thread. If **process_start** returns an error, *arg1* is closed rather
than transferred to the process being started.

## RIGHTS

TODO(ZX-2399)

## RETURN VALUE

**process_start**() returns ZX_OK on success.
In the event of failure, a negative error value is returned.

## ERRORS

**ZX_ERR_BAD_HANDLE**  *process* or *thread* or *arg1* is not a valid handle.

**ZX_ERR_WRONG_TYPE**  *process* is not a process handle or *thread* is
not a thread handle.

**ZX_ERR_ACCESS_DENIED**  The handle *thread* lacks *ZX_RIGHT_WRITE* or *thread*
does not belong to *process*, or the handle *process* lacks *ZX_RIGHT_WRITE* or
*arg1* lacks ZX_RIGHT_TRANSFER.

**ZX_ERR_BAD_STATE**  *process* is already running or has exited.

## SEE ALSO

[handle_close](handle_close.md),
[handle_duplicate](handle_duplicate.md),
[object_wait_one](object_wait_one.md),
[object_wait_many](object_wait_many.md),
[process_create](process_create.md),
[thread_create](thread_create.md),
[thread_exit](thread_exit.md),
[thread_start](thread_start.md).
