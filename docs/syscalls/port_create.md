# zx_port_create

## NAME

port_create - create an IO port

## SYNOPSIS

```
#include <zircon/syscalls.h>

zx_status_t zx_port_create(uint32_t options, zx_handle_t* out);

```

## DESCRIPTION

**port_create**() creates an port; a waitable object that can be used to
read packets queued by kernel or by user-mode.

*options* must be **0**.

The returned handle will have ZX_RIGHT_TRANSFER (allowing them to be sent
to another process via channel write), ZX_RIGHT_WRITE (allowing
packets to be queued), ZX_RIGHT_READ (allowing packets to be read) and
ZX_RIGHT_DUPLICATE (allowing them to be duplicated).

## RETURN VALUE

**port_create**() returns ZX_OK and a valid IO port handle via *out* on
success. In the event of failure, an error value is returned.

## ERRORS

**ZX_ERR_INVALID_ARGS** *options* has an invalid value, or *out* is an
invalid pointer or NULL.

**ZX_ERR_NO_MEMORY**  Failure due to lack of memory.
There is no good way for userspace to handle this (unlikely) error.
In a future build this error will no longer occur.

## SEE ALSO

[port_queue](port_queue.md),
[port_wait](port_wait.md),
[object_wait_async](object_wait_async.md),
[handle_close](handle_close.md),
[handle_duplicate](handle_duplicate.md),
[handle_replace](handle_replace.md).
