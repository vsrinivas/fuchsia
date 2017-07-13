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

**port_queue**() queues a *packet* to the port specified
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

## RETURN VALUE

**port_queue**() returns **MX_OK** on successful queue of a packet.

## ERRORS

**MX_ERR_INVALID_ARGS**  *handle* isn't a valid port handle, or
*packet* is an invalid pointer.

**MX_ERR_WRONG_TYPE** *size* is not zero or *handle* is not a port
handle.

**MX_ERR_ACCESS_DENIED**  *handle* does not have **MX_RIGHT_WRITE**.

## NOTES

The queue is drained by calling **port_wait**().


## SEE ALSO

[port_create](port_create.md).
[port_wait](port_wait.md).
