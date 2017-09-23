# zx_port_cancel

## NAME

port_cancel - cancels async port notifications on an object

## SYNOPSIS

```
#include <zircon/syscalls.h>

zx_status_t zx_port_cancel(zx_handle_t port,
                           zx_handle_t source,
                           uint64_t key);
```

## DESCRIPTION

**port_cancel**() is a non-blocking syscall which cancels
pending **object_wait_async**() calls done with *source* and *key*.

When this call succeeds no new packets from the object pointed by
*source* with *key* will be delivered to *port*, and pending queued
packets that match *source* and *key* are removed from the port.

## RETURN VALUE

**zx_port_cancel**() returns **ZX_OK** if cancellation succeeded and
either queued packets were removed or pending **object_wait_async**() were
canceled.

## ERRORS

**ZX_ERR_BAD_HANDLE**  *source* or *port* is not a valid handle.

**ZX_ERR_WRONG_TYPE**  *port* is not a port handle.

**ZX_ERR_ACCESS_DENIED**  *source* or *port* does not have **ZX_RIGHT_WRITE**.

**ZX_ERR_NOT_SUPPORTED**  *source* is a handle that cannot be waited on.

**ZX_ERR_NOT_FOUND** if either no pending packets or pending
**object_wait_async** calls with *source* and *key* were found.

## SEE ALSO

[port_wait](port_wait.md).
