# mx_socket_write

## NAME

socket_write - write data to a socket

## SYNOPSIS

```
#include <magenta/syscalls.h>

mx_status_t mx_socket_write(mx_handle_t handle, uint32_t options,
                            const void* buffer, size_t size,
                            size_t* actual) {
```

## DESCRIPTION

**socket_write**() attempts to write *size* bytes to the socket
specified by *handle*.  The pointer to *bytes* may be NULL if *size*
is zero.

If *size* is zero, a bitwise combination of **MX_SOCKET_SHUTDOWN_READ** and
**MX_SOCKET_SHUTDOWN_WRITE** can be passed to *options* to disable reading or
writing from a socket endpoint.

If **MX_SOCKET_SHUTDOWN_READ** is passed to *options*, and *size* is 0, then reading is disabled for
the socket endpoint at *handle*. All data buffered in the socket at the time of the call may be
read, but further reads from this endpoint or writes to the other endpoint of the socket will fail
with **MX_ERR_BAD_STATE**.

If **MX_SOCKET_SHUTDOWN_WRITE** is passed to *options*, and *size* is 0, then writing is disabled for
the socket endpoint at *handle*. Further writes to this endpoint or reads from the other endpoint of
the socket will fail with **MX_ERR_BAD_STATE**.

If **MX_SOCKET_CONTROL** is passed to *options*, then **socket_write**() attempts to write
into the socket control plane. A write to the control plane is never short. If the socket
control plane has insufficient space for *buffer*, it writes nothing and returns
**MX_ERR_OUT_OF_RANGE**.

If a NULL *actual* is passed in, it will be ignored.

A **MX_SOCKET_STREAM** socket write can be short if the socket does not
have enough space for all of *buffer*. The amount written is returned
via *actual*.

A **MX_SOCKET_DATAGRAM** socket write is never short. If the socket has
insufficient space for *buffer*, it writes nothing and returns
**MX_ERR_SHOULD_WAIT**.

## RETURN VALUE

**socket_write**() returns **MX_OK** on success.

## ERRORS

**MX_ERR_BAD_HANDLE**  *handle* is not a valid handle.

**MX_ERR_BAD_STATE** *options* includes **MX_SOCKET_CONTROL** and the
socket was not created with **MX_SOCKET_HAS_CONTROL**.

**MX_ERR_WRONG_TYPE**  *handle* is not a socket handle.

**MX_ERR_INVALID_ARGS**  *buffer* is an invalid pointer, or
**MX_SOCKET_HALF_CLOSE** was passed to *options* but *size* was
not 0, or *options* was not 0 or **MX_SOCKET_HALF_CLOSE**.

**MX_ERR_ACCESS_DENIED**  *handle* does not have **MX_RIGHT_WRITE**.

**MX_ERR_SHOULD_WAIT**  The buffer underlying the socket is full, or
the socket was created with **MX_SOCKET_DATAGRAM** and *buffer* is
larger than the remaining space in the socket.

**MX_ERR_BAD_STATE**  Writing has been disabled for this socket endpoint.

**MX_ERR_PEER_CLOSED**  The other side of the socket is closed.

**MX_ERR_NO_MEMORY**  (Temporary) Failure due to lack of memory.

## SEE ALSO

[socket_create](socket_create.md),
[socket_read](socket_read.md).
