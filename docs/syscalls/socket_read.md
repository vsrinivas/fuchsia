# zx_socket_read

## NAME

socket_read - read data from a socket

## SYNOPSIS

```
#include <zircon/syscalls.h>

zx_status_t zx_socket_read(zx_handle_t handle, uint32_t options,
                           void* buffer, size_t buffer_size,
                           size_t* actual) {
```

## DESCRIPTION

**socket_read**() attempts to read *buffer_size* bytes into *buffer*. If
successful, the number of bytes actually read are return via
*actual*.

If a NULL *buffer* and 0 *buffer_size* are passed in, then this syscall
instead requests that the number of outstanding bytes to be returned
via *actual*. In this case, if no bytes are available, this syscall will
return **ZX_OK**, rather than **ZX_ERR_SHOULD_WAIT**.

If a NULL *actual* is passed in, it will be ignored.

If the socket was created with **ZX_SOCKET_DATAGRAM**, this syscall reads
only the first available datagram in the socket (if one is present).
If *buffer* is too small for the datagram, then the read will be
truncated, and any remaining bytes in the datagram will be discarded.

If *options* is set to **ZX_SOCKET_CONTROL**, then **socket_read**()
attempts to read from the socket control plane.

## RIGHTS

TODO(ZX-2399)

## RETURN VALUE

**socket_read**() returns **ZX_OK** on success, and writes into
*actual* (if non-NULL) the exact number of bytes read.

## ERRORS

**ZX_ERR_BAD_HANDLE**  *handle* is not a valid handle.

**ZX_ERR_BAD_STATE** *options* includes **ZX_SOCKET_CONTROL** and the
socket was not created with **ZX_SOCKET_HAS_CONTROL**.

**ZX_ERR_WRONG_TYPE**  *handle* is not a socket handle.

**ZX_ERR_INVALID_ARGS** If any of *buffer* or *actual* are non-NULL
but invalid pointers, or if *buffer* is NULL but *size* is positive,
or if *options* is not either zero or **ZX_SOCKET_CONTROL*.

**ZX_ERR_ACCESS_DENIED**  *handle* does not have **ZX_RIGHT_READ**.

**ZX_ERR_SHOULD_WAIT**  The socket contained no data to read. (However, if a
NULL *buffer* and 0 *buffer_size* are passed in, ZX_OK will be returned in
this case.)

**ZX_ERR_PEER_CLOSED**  The other side of the socket is closed and no data is
readable.

**ZX_ERR_BAD_STATE**  Reading has been disabled for this socket endpoint.

## SEE ALSO

[socket_create](socket_create.md),
[socket_write](socket_write.md).
