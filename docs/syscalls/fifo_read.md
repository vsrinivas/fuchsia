# zx_fifo_read

## NAME

fifo_read - read data from a fifo

## SYNOPSIS

```
#include <zircon/syscalls.h>

zx_status_t zx_fifo_read(zx_handle_t handle,
                         void* buffer, size_t size,
                         uint32_t* num_entries_read);
```

## DESCRIPTION

**fifo_read**() attempts to read some number of elements out of
the fifo specified by *handle*.  *size* will be rounded down to
a multiple of the fifo's *element-size*.

It is not legal to read zero elements.

Fewer elements may be read than requested if there are insufficient
elements in the fifo to fulfill the entire request.


## RETURN VALUE

**fifo_read**() returns **ZX_OK** on success, and returns
the number of elements read (at least one) via *num_entries_read*.

## ERRORS

**ZX_ERR_BAD_HANDLE**  *handle* is not a valid handle.

**ZX_ERR_WRONG_TYPE**  *handle* is not a fifo handle.

**ZX_ERR_INVALID_ARGS**  *buffer* is an invalid pointer or *num_entries_read*
is an invalid pointer.

**ZX_ERR_OUT_OF_RANGE**  *size* was smaller than the size of a single element.

**ZX_ERR_ACCESS_DENIED**  *handle* does not have **ZX_RIGHT_READ**.

**ZX_ERR_PEER_CLOSED**  The other side of the fifo is closed.

**ZX_ERR_SHOULD_WAIT**  The fifo is empty.


## SEE ALSO

[fifo_create](fifo_create.md),
[fifo_write](fifo_write.md).
