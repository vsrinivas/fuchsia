# zx_object_get_cookie

## NAME

object_get_cookie - Get an object's cookie.

## SYNOPSIS

```
#include <zircon/syscalls.h>

zx_status_t zx_object_get_cookie(zx_handle_t handle, zx_handle_t scope, uint64_t* cookie);

```

## DESCRIPTION

Some objects (Events, Event pairs, Resources, VMOs) may have a cookie attached,
which is a 64bit opaque value.  Initially the cookie is undefined and not readable.

If the cookie has been set on an object, **zx_object_get_cookie**() may be
called, using the same object as *scope* to obtain the cookie.

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

**zx_object_get_cookie**() returns **ZX_OK** and the *cookie* value on success.
In the event of failure, a negative error value is returned.


## ERRORS

**ZX_ERR_BAD_HANDLE**  *handle* or *scope* are not valid handles.

**ZX_ERR_NOT_SUPPORTED**  *handle* is not a handle to an object that may have a cookie set.

**ZX_ERR_ACCESS_DENIED**  The cookie has not been set, or *scope* is not the correct scope
to obtain the set cookie.

**ZX_ERR_INVALID_ARGS**  *cookie* is an invalid pointer.

## SEE ALSO

[object_set_cookie](object_set_cookie.md).
