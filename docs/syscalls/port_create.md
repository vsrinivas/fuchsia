# mx_port_create

## NAME

port_create - create an IO port

## SYNOPSIS

```
#include <magenta/syscalls.h>

mx_status_t mx_port_create(uint32_t options, mx_handle_t* out);

```

## DESCRIPTION

**port_create**() creates an IO port; a waitable object that
can be used to read IO packets queued by kernel or by user-mode.

The returned handle will have MX_RIGHT_TRANSFER (allowing them to be sent
to another process via channel write), MX_RIGHT_WRITE (allowing
packets to be queued), MX_RIGHT_READ (allowing packets to be read) and
MX_RIGHT_DUPLICATE (allowing them to be duplicated).

*options* must be zero.

## RETURN VALUE

**port_create**() returns NO_ERROR and a valid IO port handle via *out* on
success. In the event of failure, an error value is returned.

## ERRORS

**ERR_INVALID_ARGS**  *options* has an invalid value.

**ERR_INVALID_ARGS**  *out* is an invalid pointer or NULL.

**ERR_NO_MEMORY**  (Temporary) Failure due to lack of memory.

## SEE ALSO

[port_queue](port_queue.md),
[port_wait](port_wait.md),
[port_bind](port_bind.md),
[handle_close](handle_close.md),
[handle_duplicate](handle_duplicate.md),
[handle_replace](handle_replace.md).
