# mx_waitset_add

## NAME

waitset_add - add an entry to a wait set

## SYNOPSIS

```
#include <magenta/syscalls.h>

mx_status_t mx_waitset_add(mx_handle_t waitset_handle,
                           uint64_t cookie,
                           mx_handle_t handle,
                           mx_signals_t signals);
```

## DESCRIPTION

**waitset_add**() adds an entry to a wait set; an entry consists of a *handle*,
a set of *signals* that the wait set will "watch", and a *cookie* to uniquely
identify the entry. Note that there may be multiple entries with the same handle
(with the same or different set of signals to watch), but that each entry must
have a distinct cookie to identify it.

*waitset_handle* must have the **MX_RIGHT_WRITE** right and *handle* must have
the **MX_RIGHT_READ** write.

## RETURN VALUE

**waitset_add**() returns **NO_ERROR** (which is zero) on success. On failure,
a (strictly) negative error value is returned.

## ERRORS

**ERR_NO_MEMORY**  (Temporary) Failure due to lack of memory.

**ERR_BAD_HANDLE**  *waitset_handle* is not a valid handle.

**ERR_INVALID_ARGS**  *waitset_handle* is not a handle to a wait set or
*handle* is not a valid handle.

**ERR_ACCESS_DENIED**  *waitset_handle* does not have the **MX_RIGHT_WRITE**
right or *handle* does not have the **MX_RIGHT_READ** right.

**ERR_NOT_SUPPORTED**  *handle* does not refer to a waitable object.

**ERR_ALREADY_EXISTS**  The wait set already has an entry with the same cookie
as *cookie*.

## SEE ALSO

[waitset_create](waitset_create.md),
[waitset_remove](waitset_remove.md),
[waitset_wait](waitset_wait.md),
[handle_close](handle_close.md).
