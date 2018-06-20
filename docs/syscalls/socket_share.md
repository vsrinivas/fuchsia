# zx_socket_share

socket_share - send another socket object via a socket

## SYNOPSIS

```
#include <zircon/syscalls.h>

zx_status_t zx_socket_share(zx_handle_t socket, zx_handle_t socket_to_send);
```

### DESCRIPTION

**socket_share**() attempts to send a new socket via an existing socket
connection.  The signal **ZX_SOCKET_SHARE** is asserted when it is possible
to send a socket.

On success, the *socket_to_send* is placed into the *socket*'s share
queue, and is no longer accessible to the caller's process. On any
failure, *socket_to_send* is discarded rather than transferred.

## RETURN VALUE

**socket_share**() returns **ZX_OK** on success.  In the event of failure,
one of the following values is returned.

## ERRORS

**ZX_ERR_BAD_HANDLE**  The handle *socket* or *socket_to_send* is invalid.

**ZX_ERR_WRONG_TYPE**  The handle *socket* or *socket_to_send* is not a socket handle.

**ZX_ERR_ACCESS_DENIED**  The handle *socket* lacks **ZX_RIGHT_WRITE** or
the handle *socket_to_send* lacks **ZX_RIGHT_TRANSFER**.

**ZX_ERR_BAD_STATE**  The *socket_to_send* was a handle to the same socket
as *socket* or to the other endpoint of *socket* or the *socket_to_send* itself
is capable of sharing.

**ZX_ERR_SHOULD_WAIT**  There is already a socket in the share queue.

**ZX_ERR_NOT_SUPPORTED**  This socket does not support the transfer of sockets.
It was not created with the ZX_SOCKET_HAS_ACCEPT option.

**ZX_ERR_PEER_CLOSED** The socket endpoint's peer is closed.

## LIMITATIONS

The socket share queue is only element deep.

## SEE ALSO

[socket_accept](socket_accept.md),
[socket_create](socket_create.md),
[socket_read](socket_read.md),
[socket_write](socket_write.md).
