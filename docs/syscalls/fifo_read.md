# zx_fifo_read

## NAME

fifo_read - read data from a fifo

## SYNOPSIS

```
#include <zircon/syscalls.h>

zx_status_t zx_fifo_read(zx_handle_t handle, size_t elem_size,
                         void* buffer, size_t count, size_t* actual_count);
```

## DESCRIPTION

**fifo_read**() attempts to read up to *count* elements from the fifo
*handle* into *buffer*.

Fewer elements may be read than requested if there are insufficient
elements in the fifo to fulfill the entire request. The number of
elements actually read is returned via *actual_count*.

The element size specified by *elem_size* must match the element size
that was passed into **fifo_create**().

*buffer* must have a size of at least *count * elem_size* bytes.

*actual_count* is allowed to be NULL. This is useful when reading
a single element: if *count* is 1 and **fifo_read**() returns **ZX_OK**,
*actual_count* is guaranteed to be 1 and thus can be safely ignored.

It is not legal to read zero elements.

## RIGHTS

*handle* must have **ZX_RIGHT_READ**.

## RETURN VALUE

**fifo_read**() returns **ZX_OK** on success, and returns
the number of elements read (at least one) via *actual_count*.

## ERRORS

**ZX_ERR_BAD_HANDLE**  *handle* is not a valid handle.

**ZX_ERR_WRONG_TYPE**  *handle* is not a fifo handle.

**ZX_ERR_INVALID_ARGS**  *buffer* is an invalid pointer or *actual_count*
is an invalid pointer.

**ZX_ERR_OUT_OF_RANGE**  *count* is zero or *elem_size* is not equal
to the element size of the fifo.

**ZX_ERR_ACCESS_DENIED**  *handle* does not have **ZX_RIGHT_READ**.

**ZX_ERR_PEER_CLOSED**  The other side of the fifo is closed.

**ZX_ERR_SHOULD_WAIT**  The fifo is empty.


## SEE ALSO

[fifo_create](fifo_create.md),
[fifo_write](fifo_write.md).
