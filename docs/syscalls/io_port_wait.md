# _magenta_io_port_wait

## NAME

io_port_wait - wait for a packet in an IO port

## SYNOPSIS

```
#include <magenta/syscalls.h>

mx_status_t _magenta_io_port_wait(mx_handle_t handle, intptr_t* key,
                                  void* packet, mx_size_t size);
```

## DESCRIPTION

**io_port_wait**() is a blocking syscall which causes the caller to
wait until at least one packet is available. *packet* must not be invalid
and *size* must be the size of **mx_user_packet_t** or the size
of **mx_io_packet_t**; both are the same size.

Upon return, if successful *packet* will contain the earliest (in FIFO order)
available packet data, and the corresponding *key* that was queued
with **io_port_queue**().

Unlike **_magenta_wait_one**() and **_magenta_wait_many**() only one waiting
thread is released (per available packet) which makes IO ports amenable to
to be serviced by thread pools.

If *key* is zero or a positive value, the *packet* is of type **mx_user_packet_t**
If *key* is a negative value, the *packet* is of type **mx_io_packet_t**.

## RETURN VALUE

**io_port_wait**() returns **NO_ERROR** on successful packet dequeuing .

## ERRORS

**ERR_INVALID_ARGS**  *handle* isn't a valid handle or *packet* isn't a valid
pointer or *size* is an invalid packet size.

**ERR_ACCESS_DENIED**  *handle* does not have **MX_RIGHT_READ** and may
not be waited upon.

## NOTES

Only kernel itself can queue packets with negative *key* values.
Positive *key* values are associated with user-mode queued packets via
**io_port_queue**().

[io_port_create](io_port_create.md).
[io_port_queue](io_port_queue.md).
[io_port_bind](io_port_bind.md).
