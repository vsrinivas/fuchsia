# zx_handle_close_many

## NAME

handle_clos - close a handle

## SYNOPSIS

```
#include <zircon/syscalls.h>

zx_status_t zx_handle_close_many(zx_handle_t* handles, size_t num_handles);
```

## DESCRIPTION

**handle_close_many**() closes a number of *handle*s, causing each
underlying object to be reclaimed by the kernel if no other handles to
it exist.

If a *handle* was used in a pending [object_wait_one](syscalls/object_wait_one.md) or a
[object_wait_many](syscalls/object_wait_many.md) call, the wait will be aborted.

It is not an error to close the special "never a valid handle" **ZX_HANDLE_INVALID**,
similar to free(NULL) being a valid call.

## RETURN VALUE

**handle_close_many**() returns **ZX_OK** on success.

## ERRORS

**ZX_ERR_BAD_HANDLE**  One of the *handles* isn't a valid handle, or the same handle is
present multiple times.

## SEE ALSO

[handle_close](handle_close.md),
[handle_duplicate](handle_duplicate.md),
[handle_replace](handle_replace.md).
