# mx_port_wait (v1)

## NAME

port_wait - wait for a packet arrival in n port, version 1.

## SYNOPSIS

```
#include <magenta/syscalls.h>
#include <magenta/syscalls/port.h>

mx_status_t mx_port_wait(mx_handle_t handle, mx_time_t deadline, void* packet, size_t size);
```

## DESCRIPTION

**port_wait**() is a blocking syscall which causes the caller to wait until at least
one packet is available. See [here](port_wait2.md) for version 2.

Upon return, if successful *packet* will contain the earliest (in FIFO order)
available packet data with **port_queue**().

The *deadline* indicates when to stop waiting for a packet (with respect to
**MX_CLOCK_MONOTONIC**).  If no packet has arrived by the deadline,
**MX_ERR_TIMED_OUT** is returned.  The value **MX_TIME_INFINITE** will
result in waiting forever.  A value in the past will result in an immediate
timeout, unless a packet is already available for reading.

Unlike **mx_wait_one**() and **mx_wait_many**() only one waiting thread is
released (per available packet) which makes ports amenable to be serviced
by thread pools.

If using **mx_port_queue**() the dequeued packet is of variable size
but always starts with **mx_packet_header_t** with *type* set to
**MX_PORT_PKT_TYPE_USER**.

```
typedef struct mx_packet_header {
    uint64_t key;
    uint32_t type;
    uint32_t extra;
} mx_packet_header_t;

```

The *key* field in the packet header is the *key* that was in the packet as send
via **mx_port_queue**(), or the *key* that was provided to **mx_port_bind**()
when the binding was made.

## RETURN VALUE

**port_wait**() returns **MX_OK** on successful packet dequeuing .

## ERRORS

**MX_ERR_INVALID_ARGS**  *handle* isn't a valid handle or *packet* isn't a valid
pointer or *size* is an invalid packet size.

**MX_ERR_ACCESS_DENIED**  *handle* does not have **MX_RIGHT_READ** and may
not be waited upon.

**MX_ERR_TIMED_OUT**  *deadline* passed and no packet was available.

## NOTES

Being able to determine which type of packet has been received (**mx_io_packet_t**
vs **mx_user_packet_t**) depends on using suitably unique keys when binding or
queuing packets.

## SEE ALSO

[port_create](port_create.md).
[port_queue](port_queue.md).
[port_bind](port_bind.md).
