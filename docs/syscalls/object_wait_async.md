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

**object_wait_async**() is a non-blocking syscall which causes packet
delivery on *port* when the object state changes and matches *signals*. Use **port_wait**() to
retrieve the packets.

*handle* points to the object that is to be watched for changes and must be a waitable object.

The *options* argument can be either:
+ **MX_WAIT_ASYNC_ONCE**: a single packet will be delivered when any of the specified *signals*
    are asserted on *handle*. To receive further packets **object_wait_async**() needs to be
    issued again.
+ **MX_WAIT_ASYNC_REPEATING**: a single packet will be delivered when any of the
    specified *signals* are asserted on *handle*. To receive further packets the previously
    enqueued packet needs to be dequeued via **port_wait**().

To stop packet delivery on either mode, close *handle* or use **port_cancel**(). For both
modes, if any of the specified signals are currently asserted on the object at the time of
the **object_wait_async**() call, a packet (or packets) will be delivered immediately.

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

## SEE ALSO

[port_cancel](port_cancel.md).
[port_queue](port_queue.md).
[port_wait](port_wait.md).
