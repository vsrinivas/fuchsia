# Socket

## NAME

Socket - Bidirectional streaming IPC transport

## SYNOPSIS

Sockets are a bidirectional stream transport. Unlike channels, sockets
only move data (not handles).

## DESCRIPTION

Data is written into one end of a socket via *mx_socket_write* and
read from the opposing end via *mx_socket_read*.

Upon creation, both ends of the socket are writable and readable. Via
the **MX_SOCKET_HALF_CLOSE** option to *mx_socket_write*, one end of
the socket can be closed for reading (and the opposing end for
writing).

## SYSCALLS

+ [socket_create](../syscalls/socket_create.md) - create a new socket
+ [socket_read](../syscalls/socket_read.md) - read data from a socket
+ [socket_write](../syscalls/socket_write.md) - write data to a socket
