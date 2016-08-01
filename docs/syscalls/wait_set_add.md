# mx_wait_set_add

## NAME

wait_set_add - add an entry to a wait set

## SYNOPSIS

```
#include <magenta/syscalls.h>

mx_status_t mx_wait_set_add(mx_handle_t wait_set_handle,
                            mx_handle_t handle,
                            mx_signals_t signals,
                            uint64_t cookie);
```

## DESCRIPTION

**wait_set_add**() adds an entry to a wait set; an entry consists of a *handle*,
a set of *signals* that the wait set will "watch", and a *cookie* to uniquely
identify the entry. Note that there may be multiple entries with the same handle
(with the same or different set of signals to watch), but that each entry must
have a distinct cookie to identify it.

*wait_set_handle* must have the **MX_RIGHT_WRITE** right and *handle* must have
the **MX_RIGHT_READ** write.

## RETURN VALUE

**wait_set_add**() returns **NO_ERROR** (which is zero) on success. On failure,
a (strictly) negative error value is returned.

## ERRORS

**ERR_NO_MEMORY**  (Temporary) Failure due to lack of memory.

**ERR_BAD_HANDLE**  *wait_set_handle* is not a valid handle.

**ERR_INVALID_ARGS**  *wait_set_handle* is not a handle to a wait set or
*handle* is not a valid handle.

**ERR_ACCESS_DENIED**  *wait_set_handle* does not have the **MX_RIGHT_WRITE**
right or *handle* does not have the **MX_RIGHT_READ** right.

**ERR_NOT_SUPPORTED**  *handle* does not refer to a waitable object.

**ERR_ALREADY_EXISTS**  The wait set already has an entry with the same cookie
as *cookie*.

## SEE ALSO

[wait_set_create](wait_set_create.md),
[wait_set_remove](wait_set_remove.md),
[wait_set_wait](wait_set_wait.md),
[handle_close](handle_close.md).
