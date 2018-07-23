# zx_process_read_memory

## NAME

process_read_memory - Read from the given process's address space.

## SYNOPSIS

```
#include <zircon/syscalls.h>

zx_status_t zx_process_read_memory(zx_handle_t process, zx_vaddr_t vaddr,
                                   void* buffer, size_t length, size_t* actual);

```

## DESCRIPTION

**zx_process_read_memory**() attempts to read memory of the specified process.

This function will eventually be replaced with something vmo-centric.

*vaddr* the address of the block of memory to read.

*buffer* pointer to a user buffer to read bytes into.

*length* number of bytes to attempt to read. *buffer* buffer must be large
enough for at least this many bytes.
*length* must be greater than zero and less than or equal to 64MB.

*actual_size* the actual number of bytes read is stored here.
Less bytes than requested may be returned if *vaddr*+*length*
extends beyond the memory mapped in the process.

## RIGHTS

TODO(ZX-2399)

## RETURN VALUE

**zx_process_read_memory**() returns **ZX_OK** on success.
In the event of failure, a negative error value is returned, and the number of
bytes written to *buffer* is undefined.

## ERRORS

**ZX_ERR_ACCESS_DENIED**  *handle* does not have the **ZX_RIGHT_READ** right
or
**ZX_WRITE_RIGHT** is needed for historical reasons.

**ZX_ERR_BAD_HANDLE**  *handle* is not a valid handle.

**ZX_ERR_BAD_STATE**  the process's memory is not accessible (e.g.,
the process is being terminated),
or the requested memory is not cacheable.

**ZX_ERR_INVALID_ARGS** *buffer* is an invalid pointer or NULL,
or *length* is zero or greater than 64MB.

**ZX_ERR_NO_MEMORY** the process does not have any memory at the
requested address.

**ZX_ERR_WRONG_TYPE**  *handle* is not a process handle.

## SEE ALSO

[process_write_memory](process_write_memory.md).
