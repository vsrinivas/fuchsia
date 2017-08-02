# mx_socket_create

## NAME

socket_create - create a socket

## SYNOPSIS

```
#include <magenta/syscalls.h>

mx_status_t mx_socket_create(uint32_t options,
                             mx_handle_t* out0, mx_handle_t* out1);

```

## DESCRIPTION

**socket_create**() creates a socket, a connected pair of
bidirectional stream transports, that can move only data, and that
have a maximum capacity.

Data written to one handle may be read from the opposite.

The *options* must set either the **MX_SOCKET_STREAM** or
**MX_SOCKET_DATAGRAM** flag. The **MX_SOCKET_HAS_CONTROL** flag
can also be set to enable the socket control plane.

## RETURN VALUE

**socket_create**() returns **MX_OK** on success. In the event of
failure, one of the following values is returned.

## ERRORS

**MX_ERR_INVALID_ARGS**  *out0* or *out1* is an invalid pointer or NULL or
*options* is any value other than 0.

**MX_ERR_NO_MEMORY**  (Temporary) Failure due to lack of memory.

## LIMITATIONS

The maximum capacity is not currently set-able or get-able.

## SEE ALSO

[socket_read](socket_read.md),
[socket_write](socket_write.md).
