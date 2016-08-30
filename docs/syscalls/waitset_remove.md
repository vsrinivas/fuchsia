# mx_waitset_remove

## NAME

waitset_remove - remove an entry from a wait set

## SYNOPSIS

```
#include <magenta/syscalls.h>

mx_status_t mx_waitset_remove(mx_handle_t waitset_handle, uint64_t cookie);
```

## DESCRIPTION

**waitset_remove**() removes an entry from a wait set (which was previously
added using **waitset_add**()).

*waitset_handle* must have the **MX_RIGHT_WRITE** right.

## RETURN VALUE

**waitset_remove**() returns **NO_ERROR** (which is zero) on success. On
failure, a (strictly) negative error value is returned.

## ERRORS

**ERR_BAD_HANDLE**  *waitset_handle* is not a valid handle.

**ERR_INVALID_ARGS**  *waitset_handle* is not a handle to a wait set.

**ERR_ACCESS_DENIED**  *waitset_handle* does not have the **MX_RIGHT_WRITE**
right.

**ERR_NOT_FOUND**  The wait set does not currently have an entry with the
specified cookie.

## SEE ALSO

[waitset_create](waitset_create.md),
[waitset_add](waitset_add.md),
[waitset_wait](waitset_wait.md),
[handle_close](handle_close.md).
