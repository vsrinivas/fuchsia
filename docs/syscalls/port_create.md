# zx_port_create

## NAME

<!-- Updated by scripts/update-docs-from-abigen, do not edit this section manually. -->

port_create - create an IO port

## SYNOPSIS

```
#include <zircon/syscalls.h>

zx_status_t zx_port_create(uint32_t options, zx_handle_t* out);

```

## DESCRIPTION

**port_create**() creates a port: a waitable object that can be used to read
packets queued by kernel or by user-mode.

If you need this port to be bound to an interrupt, pass **ZX_PORT_BIND_TO_INTERRUPT** to *options*,
otherwise it should be **0**.

In the case where a port is bound to an interrupt, the interrupt packets are delivered via a
dedicated queue on ports and are higher priority than other non-interrupt packets.

The returned handle will have:
  * `ZX_RIGHT_TRANSFER`: allowing them to be sent to another process via channel write.
  * `ZX_RIGHT_WRITE`: allowing packets to be *queued*.
  * `ZX_RIGHT_READ`: allowing packets to be *read*.
  * `ZX_RIGHT_DUPLICATE`: allowing them to be *duplicated*.

## RIGHTS

<!-- Updated by scripts/update-docs-from-abigen, do not edit this section manually. -->

TODO(ZX-2399)

## RETURN VALUE

**port_create**() returns **ZX_OK** and a valid IO port handle via *out* on
success. In the event of failure, an error value is returned.

## ERRORS

**ZX_ERR_INVALID_ARGS** *options* has an invalid value, or *out* is an
invalid pointer or NULL.

**ZX_ERR_NO_MEMORY** Failure due to lack of memory.
There is no good way for userspace to handle this (unlikely) error.
In a future builds this error will no longer occur.

## SEE ALSO

[port_queue](port_queue.md),
[port_wait](port_wait.md),
[object_wait_async](object_wait_async.md),
[handle_close](handle_close.md),
[handle_duplicate](handle_duplicate.md),
[handle_replace](handle_replace.md).
