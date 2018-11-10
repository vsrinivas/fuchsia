# zx_socket_shutdown

## NAME

socket_shutdown - prevent reading or writing

## SYNOPSIS

```
#include <zircon/syscalls.h>

zx_status_t zx_socket_shutdown(zx_handle_t handle, uint32_t options);
```

## DESCRIPTION

**socket_shutdown**() attempts to prevent future reads or writes on a socket,
where options can be a combination of **ZX_SOCKET_SHUTDOWN_READ** and
**ZX_SOCKET_SHUTDOWN_WRITE**:

 * If **ZX_SOCKET_SHUTDOWN_READ** is passed to *options*, then reading is
   disabled for the socket endpoint at *handle*. All data buffered in the socket
   at the time of the call can be read, but further reads from this endpoint or
   writes to the other endpoint of the socket will fail with
   **ZX_ERR_BAD_STATE**.

 * If **ZX_SOCKET_SHUTDOWN_WRITE** is passed to *options*, then writing is
   disabled for the socket endpoint at *handle*. Further writes to this endpoint
   or reads from the other endpoint of the socket will fail with
   **ZX_ERR_BAD_STATE**.

## RIGHTS

TODO(ZX-2399)

## RETURN VALUE

**socket_shutdown**() returns **ZX_OK** on success.

## ERRORS

**ZX_ERR_BAD_HANDLE**  *handle* is not a valid handle.

**ZX_ERR_WRONG_TYPE**  *handle* is not a socket handle.

**ZX_ERR_ACCESS_DENIED**  *handle* does not have **ZX_RIGHT_WRITE**.

**ZX_ERR_INVALID_ARGS** *options* contains an undefined flag.

## SEE ALSO

[socket_accept](socket_accept.md),
[socket_create](socket_create.md),
[socket_read](socket_write.md),
[socket_share](socket_share.md),
[socket_write](socket_write.md).
