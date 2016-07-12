# mx_io_port_create

## NAME

io_port_create - create an IO port

## SYNOPSIS

```
#include <magenta/syscalls.h>

mx_handle_t mx_io_port_create(uint32_t options);

```

## DESCRIPTION

**io_port_create**() creates an IO port; a waitable object that
can be used to read IO packets queued by kernel or by user-mode.

*options* are **MX_IOPORT_OPT_128_SLOTS** for creating an IO port with
maximum capacity of 128 outstanding packets or **MX_IOPORT_OPT_1K_SLOTS**
for creating an IO port with a 1024 outstanding packet capacity.

The returned handle will have MX_RIGHT_TRANSFER (allowing them to be sent
to another process via message pipe write), MX_RIGHT_WRITE (allowing
packets to be queued), MX_RIGHT_READ (allowing packets to be read) and
MX_RIGHT_DUPLICATE (allowing them to be duplicated).

## RETURN VALUE

**io_port_create**() returns a valid IO port handle (positive) on success.
In the event of failure, a negative error value is returned. Zero (the
"invalid handle") is never returned.

## ERRORS

**ERR_INVALID_ARGS**  *options* has an invalid value.

**ERR_NO_MEMORY**  (Temporary) Failure due to lack of memory.

## SEE ALSO

[io_port_queue](io_port_queue.md).
[io_port_wait](io_port_wait.md).
[io_port_bind](io_port_bind.md).
[handle_close](handle_close.md).
[handle_duplicate](handle_duplicate.md).

