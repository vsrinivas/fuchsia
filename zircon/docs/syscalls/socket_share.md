# zx_socket_share

## NAME

<!-- Updated by update-docs-from-abigen, do not edit. -->

socket_share - send another socket object via a socket

## SYNOPSIS

<!-- Updated by update-docs-from-abigen, do not edit. -->

```
#include <zircon/syscalls.h>

zx_status_t zx_socket_share(zx_handle_t handle, zx_handle_t socket_to_share);
```

## DESCRIPTION

`zx_socket_share()` attempts to send a new socket via an existing socket
connection.  The signal **ZX_SOCKET_SHARE** is asserted when it is possible
to send a socket.

On success, the *socket_to_share* is placed into the *handle*'s share
queue, and is no longer accessible to the caller's process. On any
failure, *socket_to_share* is discarded rather than transferred.

## RIGHTS

<!-- Updated by update-docs-from-abigen, do not edit. -->

*handle* must be of type **ZX_OBJ_TYPE_SOCKET** and have **ZX_RIGHT_WRITE**.

*socket_to_share* must be of type **ZX_OBJ_TYPE_SOCKET** and have **ZX_RIGHT_TRANSFER**.

## RETURN VALUE

`zx_socket_share()` returns **ZX_OK** on success.  In the event of failure,
one of the following values is returned.

## ERRORS

**ZX_ERR_BAD_HANDLE**  The handle *handle* or *socket_to_share* is invalid.

**ZX_ERR_WRONG_TYPE**  The handle *handle* or *socket_to_share* is not a socket handle.

**ZX_ERR_ACCESS_DENIED**  The handle *handle* lacks **ZX_RIGHT_WRITE** or
the handle *socket_to_share* lacks **ZX_RIGHT_TRANSFER**.

**ZX_ERR_BAD_STATE**  The *socket_to_share* was a handle to the same socket
as *handle* or to the other endpoint of *handle* or the *socket_to_share* itself
is capable of sharing.

**ZX_ERR_SHOULD_WAIT**  There is already a socket in the share queue.

**ZX_ERR_NOT_SUPPORTED**  This socket does not support the transfer of sockets.
It was not created with the **ZX_SOCKET_HAS_ACCEPT** option.

**ZX_ERR_PEER_CLOSED** The socket endpoint's peer is closed.

## LIMITATIONS

The socket share queue is only one element deep.

## SEE ALSO

 - [`zx_socket_accept()`]
 - [`zx_socket_create()`]
 - [`zx_socket_read()`]
 - [`zx_socket_write()`]

<!-- References updated by update-docs-from-abigen, do not edit. -->

[`zx_socket_accept()`]: socket_accept.md
[`zx_socket_create()`]: socket_create.md
[`zx_socket_read()`]: socket_read.md
[`zx_socket_write()`]: socket_write.md
