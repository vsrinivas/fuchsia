# Socket

## NAME

Socket - Bidirectional streaming IPC transport

## SYNOPSIS

Sockets are a bidirectional stream transport. Unlike channels, sockets
only move data (not handles).

## DESCRIPTION

Data is written into one end of a socket via *zx_socket_write* and
read from the opposing end via *zx_socket_read*.

Upon creation, both ends of the socket are writable and readable. Via the
**ZX_SOCKET_SHUTDOWN_READ** and **ZX_SOCKET_SHUTDOWN_WRITE** options to
*zx_socket_write*, one end of the socket can be closed for reading and/or
writing.

## SIGNALS

The following signals may be set for a socket object.

**ZX_SOCKET_READABLE** data is available to read from the socket

**ZX_SOCKET_WRITABLE** data may be written to the socket

**ZX_SOCKET_PEER_CLOSED** the other endpoint of this socket has
been closed.

**ZX_SOCKET_READ_DISABLED** reading (beyond already buffered data) is disabled
permanently for this endpoint either because of passing
**ZX_SOCKET_SHUTDOWN_READ** to this endpoint or passing
**ZX_SOCKET_SHUTDOWN_WRITE** to the peer. Reads on a socket endpoint with this
signal raised will succeed so long as there is data in the socket that was
written before reading was disabled.

**ZX_SOCKET_WRITE_DISABLED** writing is disabled permanently for this endpoing either
because of passing **ZX_SOCKET_SHUTDOWN_WRITE** to this endpoint or passing
**ZX_SOCKET_SHUTDOWN_READ** to the peer.

**ZX_SOCKET_CONTROL_READABLE** data is available to read from the
socket control plane.

**ZX_SOCKET_CONTROL_WRITABLE** data may be written to the socket control plane.

## SYSCALLS

+ [socket_create](../syscalls/socket_create.md) - create a new socket
+ [socket_read](../syscalls/socket_read.md) - read data from a socket
+ [socket_write](../syscalls/socket_write.md) - write data to a socket
