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
delivery on *port* when the object state changes and matches *signals*.

If the current signal state of the object matches *signals* a packet
will be delivered to the *port* immediately.

The *options* argument can be **MX_WAIT_ASYNC_ONCE** do deliver a single
packet or **MX_WAIT_ASYNC_REPEATING** to deliver a packet every time the
object state transitions from some other state to the *signals* state again.

To stop packet delivery, close *handle* or use **object_wait_cancel**().

## RETURN VALUE

**object_wait_async**() returns **NO_ERROR** if the subscription succeeded.

## ERRORS

**ERR_INVALID_ARGS**  *options* is not **MX_WAIT_ASYNC_ONCE** or **MX_WAIT_ASYNC_REPEATING**.

**ERR_BAD_HANDLE**  *handle* is not a valid handle or *port* is not a valid handle.

**ERR_WRONG_TYPE**  *port* is not a Port handle.

**ERR_ACCESS_DENIED**  *handle* does not have **MX_RIGHT_READ** or *port*
does not have **MX_RIGHT_WRITE**.

**ERR_NOT_SUPPORTED**  *handle* is a handle that cannot be waited on.

**ERR_NO_MEMORY**  Temporary out of memory condition.

## SEE ALSO

[object_wait_cancel](object_wait_cancel.md).
