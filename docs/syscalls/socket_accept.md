# zx_socket_accept

socket_accept - receive another socket object via a socket

## SYNOPSIS

```
#include <zircon/syscalls.h>

zx_status_t zx_socket_accept(zx_handle_t socket, zx_handle_t* out_socket);
```

### DESCRIPTION

**socket_accept**() attempts to receive a new socket via an existing socket
connection.  The signal **ZX_SOCKET_ACCEPT** is asserted when there is a new
socket available.

## RIGHTS

TODO(ZX-2399)

## RETURN VALUE

**socket_accept**() returns **ZX_OK** on success and the received handle
is returned via *out_socket*.  In the event of failure, one of the following
values is returned.

## ERRORS

**ZX_ERR_BAD_HANDLE**  The handle *socket* is invalid.

**ZX_ERR_WRONG_TYPE**  The handle *socket* is not a socket handle.

**ZX_ERR_ACCESS_DENIED**  The handle *socket* lacks **ZX_RIGHT_READ**.

**ZX_ERR_INVALID_ARGS**  *out_socket* is an invalid pointer.

**ZX_ERR_SHOULD_WAIT**  There is no new socket ready to be accepted.

**ZX_ERR_NOT_SUPPORTED**  This socket does not support the transfer of sockets.
It was not created with the **ZX_SOCKET_HAS_ACCEPT** option.

## LIMITATIONS

The socket accept queue is only one element deep.

## SEE ALSO

[socket_create](socket_create.md),
[socket_read](socket_read.md),
[socket_share](socket_share.md),
[socket_write](socket_write.md).
