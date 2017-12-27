# zx_interrupt_create

## NAME

interrupt_create - create an interrupt handle

## SYNOPSIS

```
#include <zircon/syscalls.h>

zx_status_t zx_interrupt_create(zx_handle_t resource, uint32_t options,
                                zx_handle_t* out_handle);

```

## DESCRIPTION

**interrupt_create**() creates a handle that drivers can wait for hardware or virtual interrupts on.
Virtual interrupts are software interrupts, which can be signaled via **interrupt_signal()**.

The parameter *resource* is a resource handle used to control access to this
syscall. *resource* must be the root resource.

The parameter *options* is currently unused and must be zero.

An interrupt handle is returned in the *out_handle* parameter on success.

The handles will have *ZX_RIGHT_TRANSFER* (allowing them to be sent
to another process via channel write), as well as *ZX_RIGHT_READ* and *ZX_RIGHT_WRITE*.
In particular, interrupt handles do not have *ZX_RIGHT_DUPLICATE*.

## RETURN VALUE

**interrupt_create**() returns **ZX_OK** on success. In the event
of failure, a negative error value is returned.

## ERRORS

**ZX_ERR_ACCESS_DENIED** the *resource* handle does not allow this operation.

**ZX_ERR_INVALID_ARGS** *options* contains invalid flags or the *out_handle*
parameter is an invalid pointer.

**ZX_ERR_NO_MEMORY**  (Temporary) Failure due to lack of memory.

## SEE ALSO

[interrupt_bind](interrupt_bind.md),
[interrupt_wait](interrupt_wait.md),
[interrupt_get_timestamp](interrupt_get_timestamp.md),
[interrupt_signal](interrupt_signal.md),
[handle_close](handle_close.md).
