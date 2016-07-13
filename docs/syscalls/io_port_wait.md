# mx_io_port_wait

## NAME

io_port_wait - wait for a packet in an IO port

## SYNOPSIS

```
#include <magenta/syscalls.h>

mx_status_t mx_io_port_wait(mx_handle_t handle, void* packet, mx_size_t size);
```

## DESCRIPTION

**io_port_wait**() is a blocking syscall which causes the caller to
wait until at least one packet is available. *packet* must not be invalid
and *size* must be the size of **mx_user_packet_t** or the size
of **mx_io_packet_t**; both are the same size.

Upon return, if successful *packet* will contain the earliest (in FIFO order)
available packet data, and the corresponding *key* that was queued
with **io_port_queue**().

Unlike **mx_wait_one**() and **mx_wait_many**() only one waiting thread is
released (per available packet) which makes IO ports amenable to be serviced
by thread pools.

The *key* field in the packet is the *key* that was in the packet as send
via **mx_io_port_queue**(), or the *key* that was provided to **mx_io_port_bind**()
when the binding was made.

## RETURN VALUE

**io_port_wait**() returns **NO_ERROR** on successful packet dequeuing .

## ERRORS

**ERR_INVALID_ARGS**  *handle* isn't a valid handle or *packet* isn't a valid
pointer or *size* is an invalid packet size.

**ERR_ACCESS_DENIED**  *handle* does not have **MX_RIGHT_READ** and may
not be waited upon.

## NOTES

Being able to determine which type of packet has been received (**mx_io_packet_t**
vs **mx_user_packet_t**) depends on using suitably unique keys when binding or
queueing packets.

## SEE ALSO

[io_port_create](io_port_create.md).
[io_port_queue](io_port_queue.md).
[io_port_bind](io_port_bind.md).
