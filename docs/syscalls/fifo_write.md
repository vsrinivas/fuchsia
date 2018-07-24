# zx_fifo_write

## NAME

fifo_write - write data to a fifo

## SYNOPSIS

```
#include <zircon/syscalls.h>

zx_status_t zx_fifo_write(zx_handle_t handle, size_t elem_size,
                          const void* buffer, size_t count, size_t* actual_count);
```

## DESCRIPTION

**fifo_write**() attempts to write up to *count* elements
(*count * elem_size* bytes) from *buf* to the fifo specified by *handle*.

Fewer elements may be written than requested if there is insufficient
room in the fifo to contain all of them. The number of
elements actually written is returned via *actual_count*.

The element size specified by *elem_size* must match the element size
that was passed into **fifo_create**().

*actual_count* is allowed to be NULL. This is useful when writing
a single element: if *count* is 1 and **fifo_write**() returns **ZX_OK**,
*actual_count* is guaranteed to be 1 and thus can be safely ignored.

It is not legal to write zero elements.

## RIGHTS

*handle* must have **ZX_RIGHT_WRITE**.

## RETURN VALUE

**fifo_write**() returns **ZX_OK** on success, and returns
the number of elements written (at least one) via *actual_count*.

## ERRORS

**ZX_ERR_BAD_HANDLE**  *handle* is not a valid handle.

**ZX_ERR_WRONG_TYPE**  *handle* is not a fifo handle.

**ZX_ERR_INVALID_ARGS**  *buffer* is an invalid pointer or *actual_count*
is an invalid pointer.

**ZX_ERR_OUT_OF_RANGE**  *count* is zero or *elem_size* is not equal
to the element size of the fifo.

**ZX_ERR_ACCESS_DENIED**  *handle* does not have **ZX_RIGHT_WRITE**.

**ZX_ERR_PEER_CLOSED**  The other side of the fifo is closed.

**ZX_ERR_SHOULD_WAIT**  The fifo is full.


## SEE ALSO

[fifo_create](fifo_create.md),
[fifo_read](fifo_read.md).
