# mx_port_cancel

## NAME

port_cancel - cancels async port notifications on an object

## SYNOPSIS

```
#include <magenta/syscalls.h>

mx_status_t mx_port_cancel(mx_handle_t port,
                           mx_handle_t source,
                           uint64_t key);
```

## DESCRIPTION

**port_cancel**() is a non-blocking syscall which cancels
pending **object_wait_async**() calls done with *handle* and *key*.

When this call succeeds no new packets from the object pointed by
*handle* with *key* will be delivered to *port*, and pending queued
packets that match *source* and *key* are removed from the port.

## RETURN VALUE

**mx_port_cancel**() returns **MX_OK** if cancellation succeeded and
either queued packets were removed or pending **object_wait_async**() were
canceled.

## ERRORS

**MX_ERR_BAD_HANDLE**  *handle* or *port* is not a valid handle.

**MX_ERR_WRONG_TYPE**  *port* is not a port handle.

**MX_ERR_ACCESS_DENIED**  *handle* or *port* does not have **MX_RIGHT_WRITE**.

**MX_ERR_NOT_SUPPORTED**  *handle* is a handle that cannot be waited on.

**MX_ERR_NOT_FOUND** if either no pending packets or pending
**object_wait_async** calls with *source* and *key* were found.

## SEE ALSO

[port_wait](port_wait.md).
