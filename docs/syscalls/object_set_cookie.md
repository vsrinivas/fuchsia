# zx_object_set_cookie

## NAME

object_set_cookie - Set an object's cookie.

## SYNOPSIS

```
#include <zircon/syscalls.h>

zx_status_t zx_object_set_cookie(zx_handle_t handle, zx_handle_t scope, uint64_t cookie);

```

## DESCRIPTION
Some objects (Events, Event pairs, Resources, VMOs) may have a Cookie attached,
which is a 64bit opaque value.  Initially the Cookie is undefined and not
readable.

Once **zx_object_set_cookie**() is called successfully, the cookie is set,
and the Object referenced by the *scope* handle becomes the key necessary
to read the cookie or modify it.  The *scope* may never be changed for the
lifetime of the object.

Event pairs are special.  If one side of the pair is closed, the other side's
cookie is invalidated. An invalidated cookie is not get-able or set-able with any scope.

Cookies are useful for objects that will be passed to another process and
later returned.  By setting the cookie with **zx_object_set_cookie**(),
using a *scope* that is not accessible by other processes, **zx_object_get_cookie**()
may later be used to verify that a handle is referring to an object that was
"created" by the calling process and simultaneously return an ID or pointer
to local state for that object.

When the object referenced by *scope* is destroyed or if a handle to that object
is no longer available, the cookie may no longer be modified or obtained.


## RIGHTS

TODO(ZX-2399)

## RETURN VALUE

**zx_object_set_cookie**() returns **ZX_OK** on success.  In the event of failure,
a negative error value is returned.


## ERRORS

**ZX_ERR_BAD_HANDLE**  *handle* or *scope* are not valid handles.

**ZX_ERR_NOT_SUPPORTED**  *handle* is not a handle to an object that may have a cookie set.

**ZX_ERR_ACCESS_DENIED**  **zx_object_set_cookie**() was called previously with a different
object as the *scope*, or the cookie has not been set.


## SEE ALSO

[object_get_cookie](object_get_cookie.md).
