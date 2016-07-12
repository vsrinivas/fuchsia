# mx_io_port_queue

## NAME

io_port_queue - queue a packet to an IO port

## SYNOPSIS

```
#include <magenta/syscalls.h>

mx_status_t mx_io_port_queue(mx_handle_t handle, intptr_t key,
                             const void* packet, mx_size_t size);

```

## DESCRIPTION

**io_port_queue**() attempts to queue a *packet* of *size* 
bytes to the IO port specified by *handle*. The *key* must be zero or
any positive number. The *packet* must be of type **mx_user_packet_t**
and *size* must be the size of mx_user_packet_t.

```
typedef struct mx_user_packet {
    uint64_t param[3];
} mx_user_packet_t;

```

## RETURN VALUE

**io_port_queue**() returns **NO_ERROR** on successful queue of a packet.

## ERRORS

**ERR_INVALID_ARGS**  *handle* isn't a valid IO port handle, or
*packet* is an invalid pointer, or *size* is not equal to the size
of **mx_user_packet_t** or *key* is less than zero.

**ERR_ACCESS_DENIED**  *handle* does not have **MX_RIGHT_WRITE**.

**ERR_NOT_ENOUGH_BUFFER**  The IO port is full.

## NOTES

The IO port is full if there are as many outstanding queued
packets as the capacity of the IO port.

The queue is drained by calling **io_port_wait**().

## SEE ALSO

[io_port_create](io_port_create.md).
[io_port_wait](io_port_wait.md).
[io_port_bind](io_port_bind.md).

