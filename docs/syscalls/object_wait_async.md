# mx_object_wait_async

## NAME

object_wait_async - subscribe for signals on an object

## SYNOPSIS

```
#include <magenta/syscalls.h>

mx_status_t mx_object_wait_async(mx_handle_t handle,
                                 mx_handle_t port,
                                 uint64_t key,
                                 mx_signals_t signals,
                                 uint32_t options);
```

## DESCRIPTION

**object_wait_async**() is a non-blocking syscall which causes packets to be
enqueued on *port* when the the specified condition is met.
Use **port_wait**() to retrieve the packets.

*handle* points to the object that is to be watched for changes and must be a waitable object.

The *options* argument can be either **MX_WAIT_ASYNC_ONCE** or **MX_WAIT_ASYNC_REPEATING**.

In both cases, *signals* indicates which signals on the object specified by *handle*
will cause a packet to be enqueued, and if **any** of those signals are active when
**object_wait_async**() is called, or become asserted afterwards, a packet will be
enqueued on *port*.

In the case of **MX_WAIT_ASYNC_ONCE**, once a packet has been enqueued the asynchronous
waiting ends.  No further packets will be enqueued.

In the case of **MX_WAIT_ASYNC_REPEATING** the asynchronous waiting continues until
canceled.  If any of *signals* are asserted and a packet is not currently in *port*'s
queue on behalf of this wait, a packet is enqueued.  If a packet is already in the
queue, the packet's *observed* field is updated.  This mode acts in an edge-triggered
fashion.

In either mode, **port_cancel**() will terminate the operation and if a packet was
in the queue on behalf of the operation, that packet will be removed from the queue.

If the handle is closed, the operation will also be terminated, but packets already
in the queue are not affected.

See [port_wait](port_wait.md) for more information about each type
of packet and their semantics.

## RETURN VALUE

**object_wait_async**() returns **MX_OK** if the subscription succeeded.

## ERRORS

**MX_ERR_INVALID_ARGS**  *options* is not **MX_WAIT_ASYNC_ONCE** or **MX_WAIT_ASYNC_REPEATING**.

**MX_ERR_BAD_HANDLE**  *handle* is not a valid handle or *port* is not a valid handle.

**MX_ERR_WRONG_TYPE**  *port* is not a Port handle.

**MX_ERR_ACCESS_DENIED**  *handle* does not have **MX_RIGHT_READ** or *port*
does not have **MX_RIGHT_WRITE**.

**MX_ERR_NOT_SUPPORTED**  *handle* is a handle that cannot be waited on.

**MX_ERR_NO_MEMORY**  Temporary out of memory condition.

## NOTES

See [signals](../signals.md) for more information about signals and their terminology.


## SEE ALSO

[port_cancel](port_cancel.md).
[port_queue](port_queue.md).
[port_wait](port_wait.md).

