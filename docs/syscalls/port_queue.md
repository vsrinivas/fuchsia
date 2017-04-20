# mx_port_queue

## NAME

port_queue - queue a packet to an port

## SYNOPSIS

```
#include <magenta/syscalls.h>
#include <magenta/syscalls/port.h>

mx_status_t mx_port_queue(mx_handle_t handle, const void* packet, size_t size);

```

## DESCRIPTION

Ports V2: **port_queue**() queues a *packet* to the port specified
by *handle*. The *packet* must be of type **mx_port_packet** and
*size* should be set to zero.

```
typedef struct mx_port_packet {
    uint64_t key;
    uint32_t type;
    int32_t status;
    union {
        mx_packet_user_t user;
        mx_packet_signal_t signal;
    };
} mx_port_packet_t;

```

In **mx** *type* should be MX_PKT_TYPE_USER and only the **user**
union element is considered valid:

```
typedef union mx_packet_user {
    uint64_t u64[4];
    uint32_t u32[8];
    uint16_t u16[16];
    uint8_t   c8[32];
} mx_packet_user_t;

```

Ports V1: **port_queue**() queues a *packet* of *size* bytes
to the port specified by *handle*. The *packet* must begin
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

**port_queue**() returns **MX_OK** on successful queue of a packet.

## ERRORS Ports V2

**MX_ERR_INVALID_ARGS**  *handle* isn't a valid port handle, or
*packet* is an invalid pointer.

**MX_ERR_WRONG_TYPE** *size* is not zero or *handle* is not a port V2
handle.

**MX_ERR_ACCESS_DENIED**  *handle* does not have **MX_RIGHT_WRITE**.

## ERRORS Ports V1

**MX_ERR_INVALID_ARGS**  *handle* isn't a valid port handle, or
*packet* is an invalid pointer, or *size* is less than the size
of **mx_packet_header_t**.

**MX_ERR_ACCESS_DENIED**  *handle* does not have **MX_RIGHT_WRITE**.

**MX_ERR_BUFFER_TOO_SMALL**  If the packet is too big.

## NOTES

The queue is drained by calling **port_wait**().


## SEE ALSO

[port_create](port_create.md).
[port_wait](port_wait.md).
