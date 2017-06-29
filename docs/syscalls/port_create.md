# mx_port_create

## NAME

port_create - create an IO port

## SYNOPSIS

```
#include <magenta/syscalls.h>

mx_status_t mx_port_create(uint32_t options, mx_handle_t* out);

```

## DESCRIPTION

**port_create**() creates an port; a waitable object that can be used to
read packets queued by kernel or by user-mode.

*options* must be **0**.

The returned handle will have MX_RIGHT_TRANSFER (allowing them to be sent
to another process via channel write), MX_RIGHT_WRITE (allowing
packets to be queued), MX_RIGHT_READ (allowing packets to be read) and
MX_RIGHT_DUPLICATE (allowing them to be duplicated).

## RETURN VALUE

**port_create**() returns MX_OK and a valid IO port handle via *out* on
success. In the event of failure, an error value is returned.

## ERRORS

**MX_ERR_INVALID_ARGS** *options* has an invalid value, or *out* is an
invalid pointer or NULL.

**MX_ERR_NO_MEMORY**  (Temporary) Failure due to lack of memory.

## SEE ALSO

[port_queue](port_queue.md),
[port_wait](port_wait.md),
[object_wait_async](object_wait_async.md),
[handle_close](handle_close.md),
[handle_duplicate](handle_duplicate.md),
[handle_replace](handle_replace.md).
