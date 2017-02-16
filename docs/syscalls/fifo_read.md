# mx_fifo_read

## NAME

fifo_read - read data from a fifo

## SYNOPSIS

```
#include <magenta/syscalls.h>

mx_status_t mx_fifo_read(mx_handle_t handle,
                         void* buffer, size_t size,
                         uint32_t* num_entries_read) {
```

## DESCRIPTION

**fifo_read**() attempts to read some number of elements out of
the fifo specified by *handle*.  *size* will be rounded down to
a multiple of the fifo's *element-size*.

It is not legal to read zero elements.

Fewer elements may be read than requested if there are insufficient
elements in the fifo to fulfill the entire request.


## RETURN VALUE

**fifo_read**() returns **NO_ERROR** on success, and returns
the number of elements read (at least one) via *num_entries_read*.

## ERRORS

**ERR_BAD_HANDLE**  *handle* is not a valid handle.

**ERR_WRONG_TYPE**  *handle* is not a fifo handle.

**ERR_INVALID_ARGS**  *buffer* is an invalid pointer or *num_entries_read*
is an invalid pointer.

**ERR_OUT_OF_RANGE**  *size* was smaller than the size of a single element.

**ERR_ACCESS_DENIED**  *handle* does not have **MX_RIGHT_READ**.

**ERR_PEER_CLOSED**  The other side of the fifo is closed.

**ERR_SHOULD_WAIT**  The fifo is empty.


## SEE ALSO

[fifo_create](fifo_create.md),
[fifo_write](fifo_write.md).
