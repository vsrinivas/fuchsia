# mx_datapipe_write

## NAME

datapipe_write - write data to a data pipe

## SYNOPSIS

```
#include <magenta/syscalls.h>

mx_ssize_t mx_datapipe_write(mx_handle_t producer_handle,
                             uint32_t flags,
                             mx_size_t requested,
                             const void* buffer);
```

## DESCRIPTION

**datapipe_write**() writes data to a data pipe.

*requested* is the amount of data that the caller would like to write; it must
be a multiple of the data pipe's element size. *buffer* is the source of the
data (and should have *requested* bytes available to read).

If **MX_DATAPIPE_WRITE_FLAG_ALL_OR_NONE** is set in *flags*, this either writes
the requested amount of data or none at all. Otherwise this may do a partial
write (if the data pipe has insufficient available capacity to accomodate the
entire requested write), with the amount of data written returned.

## RETURN VALUE

**datapipe_write**() returns the number of bytes written on success; this may be
less than *requested*, but will always be a multiple of the data pipe's element
size. On failure, a (strictly) negative error value is returned.

## ERRORS

**ERR_BAD_HANDLE**  *producer_handle* is not a valid handle.

**ERR_WRONG_TYPE**  *producer_handle* is not a handle to a data pipe producer.

**ERR_ACCESS_DENIED**  *producer_handle* does not have **MX_RIGHT_WRITE**.

**ERR_NOT_SUPPORTED**  *flags* has an unknown flag set.

**ERR_ALREADY_BOUND**  *producer_handle* is currently in a two-phase write.

**ERR_INVALID_ARGS**  *requested* is not a multiple of the data pipe's element
size, or *buffer* is an invalid pointer (or NULL).

**ERR_REMOTE_CLOSED**  The remote data pipe consumer handle is closed.

**ERR_SHOULD_WAIT**  The data pipe is currently full and *requested* is nonzero,
but no data could be written (and the consumer is still open).

**ERR_OUT_OF_RANGE**  *requested* is nonzero and
**MX_DATAPIPE_WRITE_FLAG_ALL_OR_NONE** is set, but the data pipe does not have
the requested amount of space available (and the consumer is still open).

## BUGS

The **ERR_OUT_OF_RANGE** will be changed to **ERR_SHOULD_WAIT** once write
thresholds are implemented (and a corresponding Mojo change is made).

## SEE ALSO

[datapipe_create](datapipe_create.md),
[datapipe_begin_write](datapipe_begin_write.md),
[datapipe_end_write](datapipe_end_write.md),
[datapipe_read](datapipe_read.md),
[datapipe_begin_read](datapipe_begin_read.md),
[datapipe_end_read](datapipe_end_read.md),
[handle_close](handle_close.md).
