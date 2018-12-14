# zx_socket_read

## NAME

<!-- Updated by update-docs-from-abigen, do not edit. -->

socket_read - read data from a socket

## SYNOPSIS

<!-- Updated by update-docs-from-abigen, do not edit. -->

```
#include <zircon/syscalls.h>

zx_status_t zx_socket_read(zx_handle_t handle,
                           uint32_t options,
                           void* buffer,
                           size_t buffer_size,
                           size_t* actual);
```

## DESCRIPTION

`zx_socket_read()` attempts to read *buffer_size* bytes into *buffer*. If
successful, the number of bytes actually read are return via
*actual*.

If a NULL *actual* is passed in, it will be ignored.

If the socket was created with **ZX_SOCKET_DATAGRAM**, this syscall reads
only the first available datagram in the socket (if one is present).
If *buffer* is too small for the datagram, then the read will be
truncated, and any remaining bytes in the datagram will be discarded.

If *options* is set to **ZX_SOCKET_CONTROL**, then `zx_socket_read()`
attempts to read from the socket control plane.

To determine how many bytes are available to read, use the **rx_buf_available**
field of the resulting `zx_info_socket_t`, which you can obtain using the
**ZX_INFO_SOCKET** topic for [`zx_object_get_info()`].

## RIGHTS

<!-- Updated by update-docs-from-abigen, do not edit. -->

*handle* must be of type **ZX_OBJ_TYPE_SOCKET** and have **ZX_RIGHT_READ**.

## RETURN VALUE

`zx_socket_read()` returns **ZX_OK** on success, and writes into
*actual* (if non-NULL) the exact number of bytes read.

## ERRORS

**ZX_ERR_BAD_HANDLE**  *handle* is not a valid handle.

**ZX_ERR_BAD_STATE** Either (a) *options* includes **ZX_SOCKET_CONTROL** and the
socket was not created with **ZX_SOCKET_HAS_CONTROL**, or (b) reading has been
disabled for this socket endpoint.

**ZX_ERR_WRONG_TYPE**  *handle* is not a socket handle.

**ZX_ERR_INVALID_ARGS** If any of *buffer* or *actual* are non-NULL
but invalid pointers, or if *buffer* is NULL, or if *options* is not either zero
or **ZX_SOCKET_CONTROL**.

**ZX_ERR_ACCESS_DENIED**  *handle* does not have **ZX_RIGHT_READ**.

**ZX_ERR_SHOULD_WAIT**  The socket contained no data to read.

**ZX_ERR_PEER_CLOSED**  The other side of the socket is closed and no data is
readable.

## SEE ALSO

 - [`zx_socket_accept()`]
 - [`zx_socket_create()`]
 - [`zx_socket_share()`]
 - [`zx_socket_shutdown()`]
 - [`zx_socket_write()`]

<!-- References updated by update-docs-from-abigen, do not edit. -->

[`zx_object_get_info()`]: object_get_info.md
[`zx_socket_accept()`]: socket_accept.md
[`zx_socket_create()`]: socket_create.md
[`zx_socket_share()`]: socket_share.md
[`zx_socket_shutdown()`]: socket_shutdown.md
[`zx_socket_write()`]: socket_write.md
