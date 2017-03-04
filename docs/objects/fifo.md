# FIFO

## NAME

FIFO - first-in first-out interprocess queue

## SYNOPSIS

FIFOs are intended to be the control plane for shared memory
transports.  Their read and write operations are more efficient than
[sockets](socket.md) or [channels](channel.md), but there are severe
restrictions on the size of elements and buffers.

## DESCRIPTION

TODO

## SYSCALLS

+ [fifo_create](../syscalls/fifo_create.md) - create a new fifo
+ [fifo_read](../syscalls/fifo_read.md) - read data from a fifo
+ [fifo_write](../syscalls/fifo_write.md) - write data to a fifo
