# zx_handle_close

## NAME

handle_close - close a handle

## SYNOPSIS

```
#include <zircon/syscalls.h>

zx_status_t zx_handle_close(zx_handle_t handle);
```

## DESCRIPTION

**handle_close**() closes a *handle*, causing the underlying object to be
reclaimed by the kernel if no other handles to it exist.

If the *handle* was used in a pending [object_wait_one](syscalls/object_wait_one.md) or a
[object_wait_many](syscalls/object_wait_many.md) call, the wait will be aborted.

It is not an error to close the special "never a valid handle" **ZX_HANDLE_INVALID**,
similar to free(NULL) being a valid call.

## RETURN VALUE

**handle_close**() returns **ZX_OK** on success.

## ERRORS

**ZX_ERR_BAD_HANDLE**  *handle* isn't a valid handle.

## SEE ALSO

[handle_duplicate](handle_duplicate.md),
[handle_replace](handle_replace.md).
