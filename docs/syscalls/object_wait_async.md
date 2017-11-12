# zx_object_wait_async

## NAME

object_wait_async - subscribe for signals on an object

## SYNOPSIS

```
#include <zircon/syscalls.h>

zx_status_t zx_object_wait_async(zx_handle_t handle,
                                 zx_handle_t port,
                                 uint64_t key,
                                 zx_signals_t signals,
                                 uint32_t options);
```

## DESCRIPTION

**object_wait_async**() is a non-blocking syscall which causes packets to be
enqueued on *port* when the the specified condition is met.
Use **port_wait**() to retrieve the packets.

*handle* points to the object that is to be watched for changes and must be a waitable object.

The *options* argument can be either **ZX_WAIT_ASYNC_ONCE** or **ZX_WAIT_ASYNC_REPEATING**.

In both cases, *signals* indicates which signals on the object specified by *handle*
will cause a packet to be enqueued, and if **any** of those signals are active when
**object_wait_async**() is called, or become asserted afterwards, a packet will be
enqueued on *port*.

In the case of **ZX_WAIT_ASYNC_ONCE**, once a packet has been enqueued the asynchronous
waiting ends.  No further packets will be enqueued.

In the case of **ZX_WAIT_ASYNC_REPEATING** the asynchronous waiting continues until
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

**object_wait_async**() returns **ZX_OK** if the subscription succeeded.

## ERRORS

**ZX_ERR_INVALID_ARGS**  *options* is not **ZX_WAIT_ASYNC_ONCE** or **ZX_WAIT_ASYNC_REPEATING**.

**ZX_ERR_BAD_HANDLE**  *handle* is not a valid handle or *port* is not a valid handle.

**ZX_ERR_WRONG_TYPE**  *port* is not a Port handle.

**ZX_ERR_ACCESS_DENIED**  *handle* does not have **ZX_RIGHT_WAIT** or *port*
does not have **ZX_RIGHT_WRITE**.

**ZX_ERR_NOT_SUPPORTED**  *handle* is a handle that cannot be waited on.

**ZX_ERR_NO_MEMORY**  Temporary out of memory condition.

## NOTES

See [signals](../signals.md) for more information about signals and their terminology.


## SEE ALSO

[port_cancel](port_cancel.md).
[port_queue](port_queue.md).
[port_wait](port_wait.md).

