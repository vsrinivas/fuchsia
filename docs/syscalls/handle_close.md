# mx_handle_close

## NAME

handle_close - close a handle

## SYNOPSIS

```
#include <magenta/syscalls.h>

mx_status_t mx_handle_close(mx_handle_t handle);
```

## DESCRIPTION

**handle_close**() closes a *handle*, causing the underlying object to be
reclaimed by the kernel if no other handles to it exist.

If the *handle* was used in a pending [object_wait_one](syscalls/object_wait_one.md) or a
[object_wait_many](syscalls/object_wait_many.md) call, the wait will be aborted.

If the *handle* was the next to last handle to the object. The last handle to the
object will assert the **MX_SIGNAL_LAST_HANDLE** signal.

It is not an error to close the special "never a valid handle" **MX_HANDLE_INVALID**,
similar to free(NULL) being a valid call.

## RETURN VALUE

**handle_close**() returns **MX_OK** on success.

## ERRORS

**MX_ERR_BAD_HANDLE**  *handle* isn't a valid handle.

## SEE ALSO

[handle_duplicate](handle_duplicate.md),
[handle_replace](handle_replace.md).
