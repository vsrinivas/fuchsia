# zx_object_wait_async

## NAME

<!-- Updated by update-docs-from-abigen, do not edit. -->

object_wait_async - subscribe for signals on an object

## SYNOPSIS

<!-- Updated by update-docs-from-abigen, do not edit. -->

```
#include <zircon/syscalls.h>

zx_status_t zx_object_wait_async(zx_handle_t handle,
                                 zx_handle_t port,
                                 uint64_t key,
                                 zx_signals_t signals,
                                 uint32_t options);
```

## DESCRIPTION

`zx_object_wait_async()` is a non-blocking syscall which causes packets to be
enqueued on *port* when the specified condition is met.
Use [`zx_port_wait()`] to retrieve the packets.

*handle* points to the object that is to be watched for changes and must be a waitable object.

The *options* argument can be either **ZX_WAIT_ASYNC_ONCE** or **ZX_WAIT_ASYNC_REPEATING**.

In both cases, *signals* indicates which signals on the object specified by *handle*
will cause a packet to be enqueued, and if **any** of those signals are asserted when
`zx_object_wait_async()` is called, or become asserted afterwards, a packet will be
enqueued on *port* containing all of the currently-asserted signals (not just the ones
listed in the *signals* argument).

In the case of **ZX_WAIT_ASYNC_ONCE**, once a packet has been enqueued the asynchronous
waiting ends.  No further packets will be enqueued.

In the case of **ZX_WAIT_ASYNC_REPEATING** the asynchronous waiting continues until
canceled. If any of *signals* are asserted and a packet is not currently in *port*'s
queue on behalf of this wait, a packet is enqueued. If a packet is already in the
queue, the packet's *observed* field is updated to include all of the currently-asserted
signals (without removing the existing signals).

In either mode, [`zx_port_cancel()`] will terminate the operation and if a packet was
in the queue on behalf of the operation, that packet will be removed from the queue.

If *handle* is closed, the operation will also be terminated, but packets already
in the queue are not affected.

Packets generated via this syscall will have *type* set to either **ZX_PKT_TYPE_SIGNAL_ONE**
or **ZX_PKT_TYPE_SIGNAL_REP**, and the union is of type `zx_packet_signal_t`:

```
typedef struct zx_packet_signal {
    zx_signals_t trigger;
    zx_signals_t observed;
    uint64_t count;
} zx_packet_signal_t;
```

*trigger* is the signals used in the call to `zx_object_wait_async()`, *observed* is the
signals actually observed, and *count* is a per object defined count of pending operations. Use
the `zx_port_packet_t`'s *key* member to track what object this packet corresponds to and
therefore match *count* with the operation.

## RIGHTS

<!-- Updated by update-docs-from-abigen, do not edit. -->

*handle* must have **ZX_RIGHT_WAIT**.

*port* must be of type **ZX_OBJ_TYPE_PORT** and have **ZX_RIGHT_WRITE**.

## RETURN VALUE

`zx_object_wait_async()` returns **ZX_OK** if the subscription succeeded.

## ERRORS

**ZX_ERR_INVALID_ARGS**  *options* is not **ZX_WAIT_ASYNC_ONCE** or **ZX_WAIT_ASYNC_REPEATING**.

**ZX_ERR_BAD_HANDLE**  *handle* is not a valid handle or *port* is not a valid handle.

**ZX_ERR_WRONG_TYPE**  *port* is not a Port handle.

**ZX_ERR_ACCESS_DENIED**  *handle* does not have **ZX_RIGHT_WAIT** or *port*
does not have **ZX_RIGHT_WRITE**.

**ZX_ERR_NOT_SUPPORTED**  *handle* is a handle that cannot be waited on.

**ZX_ERR_NO_MEMORY**  Failure due to lack of memory.
There is no good way for userspace to handle this (unlikely) error.
In a future build this error will no longer occur.

## NOTES

See [signals](../signals.md) for more information about signals and their terminology.

## SEE ALSO

 - [`zx_object_wait_many()`]
 - [`zx_object_wait_one()`]
 - [`zx_port_cancel()`]
 - [`zx_port_queue()`]
 - [`zx_port_wait()`]

<!-- References updated by update-docs-from-abigen, do not edit. -->

[`zx_object_wait_many()`]: object_wait_many.md
[`zx_object_wait_one()`]: object_wait_one.md
[`zx_port_cancel()`]: port_cancel.md
[`zx_port_queue()`]: port_queue.md
[`zx_port_wait()`]: port_wait.md
