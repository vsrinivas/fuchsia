# mx_object_get_child

## NAME

object_get_child - Given a kernel object with children objects, obtain
a handle to the child specified by the provided kernel object id.

## SYNOPSIS

```
#include <magenta/syscalls.h>

mx_status_t mx_object_get_child(mx_handle_t handle, uint64_t koid,
                                mx_rights_t rights, mx_handle_t* out);

```

## DESCRIPTION

**mx_object_get_child** attempts to find a child of the object referred to
by *handle* which has the kernel object id specified by *koid*.  If such an
object exists, and the requested *rights* are not greater than those provided
by the *handle* to the parent, a new handle to the specified child object is
returned.

*rights* may be *MX_RIGHT_SAME_RIGHTS* which will result in rights equivalent
to the those on the *handle*.

If the object is a *Process*, the *Threads* it contains may be obtained by
this call.

If the object is a *Job*, its (immediate) child *Jobs* and the *Processes*
it contains may be obtained by this call.

If the object is a *Resource*, its (immediate) child *Resources* may be
obtained by this call.


## RETURN VALUE

On success, **MX_OK** is returned and a handle to the desired child object is returned via *out*.


## ERRORS

**MX_ERR_BAD_HANDLE**  *handle* is not a valid handle.

**MX_ERR_WRONG_TYPE**  *handle* is not a *Process*, *Job*, or *Resource*.

**MX_ERR_ACCESS_DENIED**   *handle* lacks the right **MX_RIGHT_ENUMERATE** or *rights* specifies
rights that are not present on *handle*.

**MX_ERR_NOT_FOUND**  *handle* does not have a child with the kernel object id *koid*.

**MX_ERR_NO_MEMORY**  (temporary) out of memory failure.

**MX_ERR_INVALID_ARGS**  *out* is an invalid pointer.


## SEE ALSO

[handle_close](handle_close.md),
[handle_duplicate](handle_duplicate.md),
[handle_replace](handle_replace.md),
[object_get_info](object_get_info.md).
