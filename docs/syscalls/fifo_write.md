# mx_fifo_write

## NAME

fifo_write - write data to a fifo

## SYNOPSIS

```
#include <magenta/syscalls.h>

mx_status_t mx_fifo_write(mx_handle_t handle,
                          const void* buffer, size_t size,
                          uint32_t* num_entries_written) {
```

## DESCRIPTION

**fifo_write**() attempts to write some number of elements into
the fifo specified by *handle*.  *size* will be rounded down to
a multiple of the fifo's *element-size*.

It is not legal to write zero elements.

Fewer elements may be written than requested if there is insufficient
room in the fifo to contain all of them.

## RETURN VALUE

**fifo_write**() returns **NO_ERROR** on success, and returns
the number of elements written (at least one) via *num_entries_written*.

## ERRORS

**ERR_BAD_HANDLE**  *handle* is not a valid handle.

**ERR_WRONG_TYPE**  *handle* is not a fifo handle.

**ERR_INVALID_ARGS**  *buffer* is an invalid pointer or *num_entries_written*
is an invalid pointer.

**ERR_OUT_OF_RANGE**  *size* was smaller than the size of a single element.

**ERR_ACCESS_DENIED**  *handle* does not have **MX_RIGHT_WRITE**.

**ERR_PEER_CLOSED**  The other side of the fifo is closed.

**ERR_SHOULD_WAIT**  The fifo is full.


## SEE ALSO

[fifo_create](fifo_create.md),
[fifo_read](fifo_read.md).
