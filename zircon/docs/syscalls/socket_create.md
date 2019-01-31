# zx_socket_create

## NAME

<!-- Updated by update-docs-from-abigen, do not edit. -->

socket_create - create a socket

## SYNOPSIS

<!-- Updated by update-docs-from-abigen, do not edit. -->

```
#include <zircon/syscalls.h>

zx_status_t zx_socket_create(uint32_t options,
                             zx_handle_t* out0,
                             zx_handle_t* out1);
```

## DESCRIPTION

`zx_socket_create()` creates a socket, a connected pair of
bidirectional stream transports, that can move only data, and that
have a maximum capacity.

Data written to one handle may be read from the opposite.

The *options* must set either the **ZX_SOCKET_STREAM** or
**ZX_SOCKET_DATAGRAM** flag.

The **ZX_SOCKET_HAS_CONTROL** flag may be set to enable the
socket control plane.

The **ZX_SOCKET_HAS_ACCEPT** flag may be set to enable transfer
of sockets over this socket via [`zx_socket_share()`] and [`zx_socket_accept()`].

## RIGHTS

<!-- Updated by update-docs-from-abigen, do not edit. -->

TODO(ZX-2399)

## RETURN VALUE

`zx_socket_create()` returns **ZX_OK** on success. In the event of
failure, one of the following values is returned.

## ERRORS

**ZX_ERR_INVALID_ARGS**  *out0* or *out1* is an invalid pointer or NULL or
*options* is any value other than **ZX_SOCKET_STREAM** or **ZX_SOCKET_DATAGRAM**.

**ZX_ERR_NO_MEMORY**  Failure due to lack of memory.
There is no good way for userspace to handle this (unlikely) error.
In a future build this error will no longer occur.

## LIMITATIONS

The maximum capacity is not currently set-able.

## SEE ALSO

 - [`zx_socket_accept()`]
 - [`zx_socket_read()`]
 - [`zx_socket_share()`]
 - [`zx_socket_shutdown()`]
 - [`zx_socket_write()`]

<!-- References updated by update-docs-from-abigen, do not edit. -->

[`zx_socket_accept()`]: socket_accept.md
[`zx_socket_read()`]: socket_read.md
[`zx_socket_share()`]: socket_share.md
[`zx_socket_shutdown()`]: socket_shutdown.md
[`zx_socket_write()`]: socket_write.md
