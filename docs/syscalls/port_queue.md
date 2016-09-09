# mx_port_queue

## NAME

port_queue - queue a packet to an IO port

## SYNOPSIS

```
#include <magenta/syscalls.h>

mx_status_t mx_port_queue(mx_handle_t handle, const void* packet, mx_size_t size);

```

## DESCRIPTION

**port_queue**() attempts to queue a *packet* of *size*
bytes to the IO port specified by *handle*. The *packet* must begin
with **mx_packet_header_t** and *size* must be the no bigger than
**MX_PORT_MAX_PKT_SIZE**.

```
typedef struct mx_packet_header {
    uint64_t key;
    uint32_t type;
    uint32_t extra;
} mx_packet_header_t;

```
*key* and *exta* values will be preserved and *type* value will be
internally recorded as **MX_PORT_PKT_TYPE_USER** to signal the
consumer of the packet that the packet comes from the **port_queue**()
operation as opposed to packets generated from bound handles which
have the type set to MX_PORT_PKT_TYPE_IOSN.

## RETURN VALUE

**port_queue**() returns **NO_ERROR** on successful queue of a packet.

## ERRORS

**ERR_INVALID_ARGS**  *handle* isn't a valid IO port handle, or
*packet* is an invalid pointer, or *size* is less than the size
of **mx_packet_header_t**.

**ERR_ACCESS_DENIED**  *handle* does not have **MX_RIGHT_WRITE**.

**ERR_BUFFER_TOO_SMALL**  If the packet is too big.

## NOTES

The queue is drained by calling **port_wait**().


## SEE ALSO

[port_create](port_create.md).
[port_wait](port_wait.md).
[port_bind](port_bind.md).

