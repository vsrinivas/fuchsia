# zx_socket_accept

## NAME

<!-- Updated by update-docs-from-abigen, do not edit. -->

socket_accept - receive another socket object via a socket

## SYNOPSIS

<!-- Updated by update-docs-from-abigen, do not edit. -->

```
#include <zircon/syscalls.h>

zx_status_t zx_socket_accept(zx_handle_t handle, zx_handle_t* out_socket);
```

## DESCRIPTION

`zx_socket_accept()` attempts to receive a new socket via an existing socket
connection.  The signal **ZX_SOCKET_ACCEPT** is asserted when there is a new
socket available.

## RIGHTS

<!-- Updated by update-docs-from-abigen, do not edit. -->

*handle* must be of type **ZX_OBJ_TYPE_SOCKET** and have **ZX_RIGHT_READ**.

## RETURN VALUE

`zx_socket_accept()` returns **ZX_OK** on success and the received handle
is returned via *out_socket*.  In the event of failure, one of the following
values is returned.

## ERRORS

**ZX_ERR_BAD_HANDLE**  *socket* is invalid.

**ZX_ERR_WRONG_TYPE**  *socket* is not a socket handle.

**ZX_ERR_ACCESS_DENIED**  *socket* lacks **ZX_RIGHT_READ**.

**ZX_ERR_INVALID_ARGS**  *out_socket* is an invalid pointer.

**ZX_ERR_SHOULD_WAIT**  There is no new socket ready to be accepted.

**ZX_ERR_NOT_SUPPORTED**  This socket does not support the transfer of sockets.
It was not created with the **ZX_SOCKET_HAS_ACCEPT** option.

## LIMITATIONS

The socket accept queue is only one element deep.

## SEE ALSO

 - [`zx_socket_create()`]
 - [`zx_socket_read()`]
 - [`zx_socket_share()`]
 - [`zx_socket_shutdown()`]
 - [`zx_socket_write()`]

<!-- References updated by update-docs-from-abigen, do not edit. -->

[`zx_socket_create()`]: socket_create.md
[`zx_socket_read()`]: socket_read.md
[`zx_socket_share()`]: socket_share.md
[`zx_socket_shutdown()`]: socket_shutdown.md
[`zx_socket_write()`]: socket_write.md
