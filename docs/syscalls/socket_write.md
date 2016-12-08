# mx_socket_write

## NAME

socket_write - write data to a socket

## SYNOPSIS

```
#include <magenta/syscalls.h>

mx_status_t mx_socket_write(mx_handle_t handle, uint32_t flags,
                            const void* buffer, size_t size,
                            size_t* actual) {
```

## DESCRIPTION

**socket_write**() attempts to write *size* bytes to the socket
specified by *handle*.  The pointer to *bytes* may be NULL if *size*
is zero.

There is one value (besides 0) that may be passed to *flags*. If
**MX_SOCKET_HALF_CLOSE** is passed to flags, and *size* is 0, then the
socket endpoint at *handle* is closed. Further writes to the other
endpoint of the socket will fail with **ERR_BAD_STATE**.

## RETURN VALUE

**socket_write**() returns **NO_ERROR** on success.

## ERRORS

**ERR_BAD_HANDLE**  *handle* is not a valid handle.

**ERR_WRONG_TYPE**  *handle* is not a socket handle.

**ERR_INVALID_ARGS**  *buffer* is an invalid pointer, or
**MX_SOCKET_HALF_CLOSE** was passed to *flags* but *size* was
not 0.

**ERR_ACCESS_DENIED**  *handle* does not have **MX_RIGHT_WRITE**.

**ERR_BAD_STATE**  The other side of the socket is closed.

**ERR_NO_MEMORY**  (Temporary) Failure due to lack of memory.

**ERR_TOO_BIG** *size* is larger than the largest allowable size for
socket messages.

## SEE ALSO

[socket_create](socket_create.md),
[socket_read](socket_read.md).
