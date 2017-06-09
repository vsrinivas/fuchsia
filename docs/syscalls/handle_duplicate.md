# mx_handle_duplicate

## NAME

handle_duplicate - duplicate a handle

## SYNOPSIS

```
#include <magenta/syscalls.h>

mx_status_t mx_handle_duplicate(mx_handle_t handle, mx_rights_t rights, mx_handle_t* out);
```

## DESCRIPTION

**handle_duplicate**() creates a duplicate of *handle*, referring
to the same underlying object, with new access rights *rights*.

To duplicate the handle with the same rights use **MX_RIGHT_SAME_RIGHTS**. If different
rights are desired they must be strictly lesser than of the source handle. It is possible
to specify no rights by using 0.

## RETURN VALUE

**handle_duplicate**() returns MX_OK and the duplicate handle via *out* on success.

## ERRORS

**MX_ERR_BAD_HANDLE**  *handle* isn't a valid handle.

**MX_ERR_INVALID_ARGS**  The *rights* requested are not a subset of *handle* rights or
*out* is an invalid pointer.

**MX_ERR_ACCESS_DENIED**  *handle* does not have **MX_RIGHT_DUPLICATE** and may not be duplicated.

**MX_ERR_NO_MEMORY**  (Temporary) out of memory situation.

## SEE ALSO

[handle_close](handle_close.md),
[handle_replace](handle_replace.md),
[rights](../rights.md).
