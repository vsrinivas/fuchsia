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

## PROPERTIES

The following properties may be queried from a socket object:

**ZX_PROP_SOCKET_RX_BUF_MAX** maximum size of the receive buffer of a socket, in
bytes. The receive buffer may become full at a capacity less than the maximum
due to overheads.

**ZX_PROP_SOCKET_RX_BUF_SIZE** size of the receive buffer of a socket, in bytes.

**ZX_PROP_SOCKET_TX_BUF_MAX** maximum size of the transmit buffer of a socket,
in bytes. The transmit buffer may become full at a capacity less than the
maximum due to overheads.

**ZX_PROP_SOCKET_TX_BUF_SIZE** size of the transmit buffer of a socket, in
bytes.

From the point of view of a socket handle, the receive buffer contains the data
that is readable via **zx_socket_read**() from that handle (having been written
from the opposing handle), and the transmit buffer contains the data that is
written via **zx_socket_write**() to that handle (and readable from the opposing
handle).

## SIGNALS

The following signals may be set for a socket object:

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

**ZX_SOCKET_WRITE_DISABLED** writing is disabled permanently for this endpoint
either because of passing **ZX_SOCKET_SHUTDOWN_WRITE** to this endpoint or
passing **ZX_SOCKET_SHUTDOWN_READ** to the peer.

**ZX_SOCKET_CONTROL_READABLE** data is available to read from the
socket control plane.

**ZX_SOCKET_CONTROL_WRITABLE** data may be written to the socket control plane.

**ZX_SOCKET_SHARE** a socket may be sent via *zx_socket_share*.

**ZX_SOCKET_ACCEPT** a socket may be received via *zx_socket_accept*.

## SYSCALLS

+ [socket_accept](../syscalls/socket_accept.md) - receive a socket via a socket
+ [socket_create](../syscalls/socket_create.md) - create a new socket
+ [socket_read](../syscalls/socket_read.md) - read data from a socket
+ [socket_share](../syscalls/socket_share.md) - share a socket via a socket
+ [socket_write](../syscalls/socket_write.md) - write data to a socket
