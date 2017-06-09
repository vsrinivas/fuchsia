# mx_fifo_create

## NAME

fifo_create - create a fifo

## SYNOPSIS

```
#include <magenta/syscalls.h>

mx_status_t mx_fifo_create(uint32_t elem_count, uint32_t elem_size,
                           uint32_t options,
                           mx_handle_t* out0, mx_handle_t* out1);

```

## DESCRIPTION

**fifo_create**() creates a fifo, which is actually a pair of fifos
of *elem_count* entries of *elem_size* bytes.  Two endpoints are
returned.  Writing to one endpoint enqueus an element into the fifo
that the opposing endpoint reads from.

Fifos are intended to be the control plane for shared memory transports.
Their read and write operations are more efficient than *sockets* or
*channels*, but there are severe restrictions on the size of elements
and buffers.

The *elem_count* must be a power of two.  The total size of each fifo
(*elem_count* * *elem_size*) may not exceed 4096 bytes.

The *options* argument must be 0.

## RETURN VALUE

**fifo_create**() returns **MX_OK** on success. In the event of
failure, one of the following values is returned.

## ERRORS

**MX_ERR_INVALID_ARGS**  *out0* or *out1* is an invalid pointer or NULL or
*options* is any value other than 0.

**MX_ERR_OUT_OF_RANGE**  *elem_count* or *elem_size* is zero, or *elem_count*
is not a power of two, or *elem_count* * *elem_size* is greater than 4096.

**MX_ERR_NO_MEMORY**  (Temporary) Failure due to lack of memory.


## SEE ALSO

[fifo_read](fifo_read.md),
[fifo_write](fifo_write.md).
