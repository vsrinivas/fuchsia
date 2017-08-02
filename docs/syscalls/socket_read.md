# mx_socket_read

## NAME

socket_read - read data from a socket

## SYNOPSIS

```
#include <magenta/syscalls.h>

mx_status_t mx_socket_read(mx_handle_t handle, uint32_t options,
                           void* buffer, size_t size,
                           size_t* actual) {
```

## DESCRIPTION

**socket_read**() attempts to read *size* bytes into *buffer*. If
successful, the number of bytes actually read are return via
*actual*.

If a NULL *buffer* and 0 *size* are passed in, then this syscall
instead requests that the number of outstanding bytes to be returned
via *actual*.

If a NULL *actual* is passed in, it will be ignored.

If the socket was created with **MX_SOCKET_DATAGRAM** and *buffer*
is too small for the packet, then the packet will be truncated,
and any remaining bytes in the packet are discarded.

If *options* is set to **MX_SOCKET_CONTROL**, then **socket_read**()
attempts to read from the socket control plane.

## RETURN VALUE

**socket_read**() returns **MX_OK** on success, and writes into
*actual* (if non-NULL) the exact number of bytes read.

## ERRORS

**MX_ERR_BAD_HANDLE**  *handle* is not a valid handle.

**MX_ERR_BAD_STATE** *options* includes **MX_SOCKET_CONTROL** and the
socket was not created with **MX_SOCKET_HAS_CONTROL**.

**MX_ERR_WRONG_TYPE**  *handle* is not a socket handle.

**MX_ERR_INVALID_ARGS** If any of *buffer* or *actual* are non-NULL
but invalid pointers, or if *buffer* is NULL but *size* is positive,
or if *options* is not either zero or **MX_SOCKET_CONTROL*.

**MX_ERR_ACCESS_DENIED**  *handle* does not have **MX_RIGHT_READ**.

**MX_ERR_SHOULD_WAIT**  The socket contained no data to read.

**MX_ERR_PEER_CLOSED**  The other side of the socket is closed and no data is
readable.

**MX_ERR_BAD_STATE**  Reading has been disabled for this socket endpoint.

**MX_ERR_NO_MEMORY**  (Temporary) Failure due to lack of memory.

## SEE ALSO

[socket_create](socket_create.md),
[socket_write](socket_write.md).
