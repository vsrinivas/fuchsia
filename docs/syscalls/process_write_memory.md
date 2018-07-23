# zx_process_write_memory

## NAME

process_write_memory - Write into the given process's address space.

## SYNOPSIS

```
#include <zircon/syscalls.h>

zx_status_t zx_process_write_memory(zx_handle_t handle, zx_vaddr_t vaddr,
                                    const void* buffer, size_t length, size_t* actual);

```

## DESCRIPTION

**zx_process_write_memory**() attempts to write memory of the specified process.

This function will eventually be replaced with something vmo-centric.

*vaddr* the address of the block of memory to write.

*buffer* pointer to a user buffer containing the bytes to write.

*length* number of bytes to attempt to write. *buffer* buffer must be large
enough for at least this many bytes.
*length* must be greater than zero and less than or equal to 64MB.

*actual_size* the actual number of bytes written is stored here.
Less bytes than requested may be returned if *vaddr*+*length*
extends beyond the memory mapped in the process.

## RIGHTS

TODO(ZX-2399)

## RETURN VALUE

**zx_process_write_memory**() returns **ZX_OK** on success.
In the event of failure, a negative error value is returned, and the number of
bytes written to *buffer* is undefined.

## ERRORS

**ZX_ERR_ACCESS_DENIED**  *handle* does not have the **ZX_RIGHT_WRITE** right.

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

[process_read_memory](process_read_memory.md).
