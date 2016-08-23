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

To unbind a *source* from an IO port, simply close the *source* handle.

## RETURN VALUE

**io_port_bind**() returns **NO_ERROR** on successful IO port bind.

## ERRORS

**ERR_INVALID_ARGS**  *handle* isn't a valid IO port handle, or *source* is an
invalid handle or *source* is not a waitable handle or *signals* is zero.

**ERR_ACCESS_DENIED** *handle* does not have **MX_RIGHT_WRITE**, or *source*
does not have **MX_RIGHT_READ** right.

## SEE ALSO

[io_port_create](io_port_create.md).
[io_port_wait](io_port_wait.md).
[io_port_queue](io_port_queue.md).
