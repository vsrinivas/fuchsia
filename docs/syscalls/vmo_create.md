# mx_vmo_create

## NAME

vmo_create - create a VM object

## SYNOPSIS

```
#include <magenta/syscalls.h>

mx_status_t mx_vmo_create(uint64_t size, uint32_t options, mx_handle_t* out);

```

## DESCRIPTION

**vmo_create**() creates a new virtual memory object (VMO), which represents
a container of zero to *size* bytes of memory managed by the operating
system.

One handle is returned on success, representing an object with the requested
size.

The following rights will be set on the handle by default:

**MX_RIGHT_DUPLICATE** - The handle may be duplicated.

**MX_RIGHT_TRANSFER** - The handle may be transferred to another process.

**MX_RIGHT_READ** - May be read from or mapped with read permissions.

**MX_RIGHT_WRITE** - May be written to or mapped with write permissions.

**MX_RIGHT_EXECUTE** - May be mapped with execute permissions.

**MX_RIGHT_MAP** - May be mapped.

**MX_RIGHT_GET_PROPERTY** - May get its properties using
[object_get_property](object_get_property).

**MX_RIGHT_SET_PROPERTY** - May set its properties using
[object_set_property](object_set_property).

The *options* field is currently unused and must be set to 0.

## RETURN VALUE

**vmo_create**() returns **MX_OK** on success. In the event
of failure, a negative error value is returned.

## ERRORS

**MX_ERR_INVALID_ARGS**  *out* is an invalid pointer or NULL or *options* is
any value other than 0.

**MX_ERR_NO_MEMORY**  Failure due to lack of memory.

## SEE ALSO

[vmo_read](vmo_read.md),
[vmo_write](vmo_write.md),
[vmo_set_size](vmo_set_size.md),
[vmo_get_size](vmo_get_size.md),
[vmo_op_range](vmo_op_range.md),
[vmar_map](vmar_map.md).
