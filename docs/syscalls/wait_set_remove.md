# mx_wait_set_remove

## NAME

wait_set_remove - remove an entry from a wait set

## SYNOPSIS

```
#include <magenta/syscalls.h>

mx_status_t mx_wait_set_remove(mx_handle_t wait_set_handle, uint64_t cookie);
```

## DESCRIPTION

**wait_set_remove**() removes an entry from a wait set (which was previously
added using **wait_set_add**()).

*wait_set_handle* must have the **MX_RIGHT_WRITE** right.

## RETURN VALUE

**wait_set_remove**() returns **NO_ERROR** (which is zero) on success. On
failure, a (strictly) negative error value is returned.

## ERRORS

**ERR_BAD_HANDLE**  *wait_set_handle* is not a valid handle.

**ERR_INVALID_ARGS**  *wait_set_handle* is not a handle to a wait set.

**ERR_ACCESS_DENIED**  *wait_set_handle* does not have the **MX_RIGHT_WRITE**
right.

**ERR_NOT_FOUND**  The wait set does not currently have an entry with the
specified cookie.

## SEE ALSO

[wait_set_create](wait_set_create.md),
[wait_set_add](wait_set_add.md),
[wait_set_wait](wait_set_wait.md),
[handle_close](handle_close.md).
