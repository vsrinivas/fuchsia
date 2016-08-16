# mx_io_port_bind

## NAME

io_port_bind - bind an IO port to another kernel object.

## SYNOPSIS

```
#include <magenta/syscalls.h>

mx_status_t mx_io_port_bind(mx_handle_t handle, uint64_t key,
                            mx_handle_t source, mx_signals_t signals);
```

## DESCRIPTION

**io_port_bind**() binds the waitable kernel object *source* to the IO port
identified by *handle*. Whenever *source* signals match any of *signals*, the
magenta kernel queues a packet of type **mx_io_packet_t** to the IO port with
the key *key* and *type* equal to MX_IO_PORT_PKT_TYPE_IOSN.

If the *source* handle was previously bound to an IO port, that bind is
revoked and replaced with a bind to *handle*.

Use *signals* with a value of zero to unbind an existing IO port bind between
*handle* and *source*.

## RETURN VALUE

**io_port_bind**() returns **NO_ERROR** on successful IO port bind.

## ERRORS

**ERR_INVALID_ARGS**  *handle* isn't a valid IO port handle, or *source* is an
invalid handle or *source* is not a waitable handle.

**ERR_ACCESS_DENIED** *handle* does not have **MX_RIGHT_WRITE**, or *source*
does not have **MX_RIGHT_READ** right.

**ERR_NOT_READY** if *source* handle is currently being used in a
**handle_wait_many**() or **handle_wait_one**() wait syscall.

## NOTES

Waitable objects are: events, processes, threads and message pipes. Once
a waitable object is bound to an IO port, **handle_wait_many**() style waits
will fail until the IO port is unbound.

## SEE ALSO

[io_port_create](io_port_create.md).
[io_port_wait](io_port_wait.md).
[io_port_queue](io_port_queue.md).

