# Socket

## NAME

Socket - Bidirectional streaming IPC transport

## SYNOPSIS

Sockets are a bidirectional stream transport. Unlike channels, sockets
only move data (not handles).

## DESCRIPTION

Data is written into one end of a socket via *mx_socket_write* and
read from the opposing end via *mx_socket_read*.

Upon creation, both ends of the socket are writable and readable. Via the
**MX_SOCKET_SHUTDOWN_READ** and **MX_SOCKET_SHUTDOWN_WRITE** options to
*mx_socket_write*, one end of the socket can be closed for reading and/or
writing.

## SIGNALS

The following signals may be set for a socket object.

**MX_SOCKET_READABLE** data is available to read from the socket

**MX_SOCKET_WRITABLE** data may be written to the socket

**MX_SOCKET_PEER_CLOSED** the other endpoint of this socket has
been closed.

**MX_SOCKET_READ_DISABLED** reading (beyond already buffered data) is disabled
permanently for this endpoint either because of passing
**MX_SOCKET_SHUTDOWN_READ** to this endpoint or passing
**MX_SOCKET_SHUTDOWN_WRITE** to the peer. Reads on a socket endpoint with this
signal raised will succeed so long as there is data in the socket that was
written before reading was disabled.

**MX_SOCKET_WRITE_DISABLED** writing is disabled permanently for this endpoing either
because of passing **MX_SOCKET_SHUTDOWN_WRITE** to this endpoint or passing
**MX_SOCKET_SHUTDOWN_READ** to the peer.

**MX_SOCKET_CONTROL_READABLE** data is available to read from the
socket control plane.

**MX_SOCKET_CONTROL_WRITABLE** data may be written to the socket control plane.

## SYSCALLS

+ [socket_create](../syscalls/socket_create.md) - create a new socket
+ [socket_read](../syscalls/socket_read.md) - read data from a socket
+ [socket_write](../syscalls/socket_write.md) - write data to a socket
