# zx_channel_read  - zx_channel_read_etc

## NAME

channel_read - read a message from a channel

## SYNOPSIS

```
#include <zircon/syscalls.h>

zx_status_t zx_channel_read(zx_handle_t handle, uint32_t options,
                            void* bytes, zx_handle_t* handles,
                            uint32_t num_bytes, uint32_t num_handles,
                            uint32_t* actual_bytes, uint32_t* actual_handles);


zx_status_t zx_channel_read_etc(zx_handle_t handle, uint32_t options,
                                void* bytes, zx_handle_info_t* handles,
                                uint32_t num_bytes, uint32_t num_handles,
                                uint32_t* actual_bytes, uint32_t* actual_handles);
```

## DESCRIPTION

**channel_read**() and **channel_read_etc**() attempts to read the first
message from the channel specified by *handle* into the provided *bytes*
and/or *handles* buffers.

The parameters *num_bytes* and *num_handles* are used to specify the
size of the respective read buffers.

Channel messages may contain both byte data and handle payloads and may
only be read in their entirety.  Partial reads are not possible.

The *bytes* buffer is written before the *handles* buffer. In the event of
overlap between these two buffers, the contents written to *handles*
will overwrite the portion of *bytes* it overlaps.

Both forms of read behave the same except that **channel_read**() returns an
array of raw ``zx_handle_t`` handle values while **channel_read_etc**() returns
an array of ``zx_handle_info_t`` structures of the form:

```
typedef struct {
    zx_handle_t handle;     // handle value
    zx_obj_type_t type;     // type of object, see ZX_OBJ_TYPE_
    zx_rights_t rights;     // handle rights
    uint32_t unused;        // set to zero
} zx_handle_info_t;
```

When communicating to an untrusted party over a channel, it is recommended
that the **channel_read_etc**() form is used and each handle type and rights
are validated against the expected values.

## RETURN VALUE

Both forms of read returns **ZX_OK** on success, if *actual_bytes*
and *actual_handles* (if non-NULL), contain the exact number of bytes
and count of handles read.

## ERRORS

**ZX_ERR_BAD_HANDLE**  *handle* is not a valid handle.

**ZX_ERR_WRONG_TYPE**  *handle* is not a channel handle.

**ZX_ERR_INVALID_ARGS**  If any of *bytes*, *handles*, *actual_bytes*, or
*actual_handles* are non-NULL and an invalid pointer.

**ZX_ERR_ACCESS_DENIED**  *handle* does not have **ZX_RIGHT_READ**.

**ZX_ERR_SHOULD_WAIT**  The channel contained no messages to read.

**ZX_ERR_PEER_CLOSED**  The other side of the channel is closed.

**ZX_ERR_NO_MEMORY**  (Temporary) Failure due to lack of memory.

**ZX_ERR_BUFFER_TOO_SMALL**  The provided *bytes* or *handles* buffers
are too small (in which case, the minimum sizes necessary to receive
the message will be written to *actual_bytes* and *actual_handles*,
provided they are non-NULL). If *options* has **ZX_CHANNEL_READ_MAY_DISCARD**
set, then the message is discarded.

## NOTES

*num_handles* and *actual_handles* are counts of the number of elements
in the *handles* array, not its size in bytes.

## SEE ALSO

[handle_close](handle_close.md),
[handle_duplicate](handle_duplicate.md),
[handle_replace](handle_replace.md),
[object_wait_one](object_wait_one.md),
[object_wait_many](object_wait_many.md),
[channel_call](channel_call.md),
[channel_create](channel_create.md),
[channel_write](channel_write.md).
