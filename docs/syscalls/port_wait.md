# mx_port_wait

## NAME

port_wait - wait for a packet in an IO port

## SYNOPSIS

```
#include <magenta/syscalls.h>

mx_status_t mx_port_wait(mx_handle_t handle, void* packet, mx_size_t size);
```

## DESCRIPTION

**port_wait**() is a blocking syscall which causes the caller to
wait until at least one packet is available. *packet* must not be invalid.

Upon return, if successful *packet* will contain the earliest (in FIFO order)
available packet data with **port_queue**().

Unlike **mx_wait_one**() and **mx_wait_many**() only one waiting thread is
released (per available packet) which makes IO ports amenable to be serviced
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

If using **port_bind**() the dequeued packet is of type **mx_io_packet_t**
with *hdr.type* set to **MX_PORT_PKT_TYPE_IOSN**.

```
typedef struct mx_io_packet {
    mx_packet_header_t hdr;
    mx_time_t timestamp;
    mx_size_t bytes;
    mx_signals_t signals;
    uint32_t reserved;
} mx_io_packet_t;

```

The *key* field in the packet header is the *key* that was in the packet as send
via **mx_port_queue**(), or the *key* that was provided to **mx_port_bind**()
when the binding was made.

## RETURN VALUE

**port_wait**() returns **NO_ERROR** on successful packet dequeuing .

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

[port_create](port_create.md).
[port_queue](port_queue.md).
[port_bind](port_bind.md).
