# mx_handle_cancel

## NAME

handle_cancel - cancels waits or async operations on an object

## SYNOPSIS

```
#include <magenta/syscalls.h>

mx_status_t mx_handle_cancel(mx_handle_t handle,
                             uint64_t key,
                             uint32_t options);
```

## DESCRIPTION

**handle_cancel**() is a non-blocking syscall which aborts
**object_wait_one**() or **object_wait_many**() calls which are pending
via *handle*. It can also cancel pending **object_wait_async**() calls.

The *options* argument can be **MX_CANCEL_ANY** to cancel any pending
wait syscall or async operation done via *handle*, or it can be **MX_CANCEL_KEY** to
cancel only async waits which where done with *handle* and *key*.

## RETURN VALUE

**mx_handle_cancel**() returns **NO_ERROR** if cancellation succeeded.

## ERRORS

**ERR_INVALID_ARGS**  *options* is not **MX_CANCEL_ANY** or **MX_CANCEL_KEY**.

**ERR_BAD_HANDLE**  *handle* is not a valid handle.

**ERR_ACCESS_DENIED**  *handle* does not have **MX_RIGHT_WRITE**.

**ERR_NOT_SUPPORTED**  *handle* is a handle that cannot be waited on.

## SEE ALSO

[object_wait_one](object_wait_one.md).
[object_wait_many](object_wait_many.md).
[object_wait_async](object_wait_async.md).
