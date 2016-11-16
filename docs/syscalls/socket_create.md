# mx_socket_create

## NAME

socket_create - create a socket

## SYNOPSIS

```
#include <magenta/syscalls.h>

mx_status_t mx_socket_create(uint32_t flags,
                             mx_handle_t* out0, mx_handle_t* out1);

```

## DESCRIPTION

**socket_create**() creates a socket, a connected pair of
bidirectional stream transports, that can move only data, and that
have a maximum capacity.

Data written to one handle may be read from the opposite.

The *flags* must currently be 0.

## RETURN VALUE

**socket_create**() returns **NO_ERROR** on success. In the event of
failure, one of the following values is returned.

## ERRORS

**ERR_INVALID_ARGS**  *out0* or *out1* is an invalid pointer or NULL or
*flags* is any value other than 0 or *MX_SOCKET_CREATE_REPLY_SOCKET*.

**ERR_NO_MEMORY**  (Temporary) Failure due to lack of memory.

## LIMITATIONS

Sockets currently only support byte streams.  An option to support
datagrams is likely in the future.

The maximum capacity is not currently set-able or get-able.

## SEE ALSO

[socket_read](socket_read.md),
[socket_write](socket_write.md).
