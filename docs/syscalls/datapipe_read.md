# mx_datapipe_read

## NAME

datapipe_read - read (or discard, or query, or peek) data from a data pipe

## SYNOPSIS

```
#include <magenta/syscalls.h>

mx_ssize_t mx_datapipe_read(mx_handle_t consumer_handle,
                            uint32_t flags,
                            mx_size_t requested,
                            void* buffer);
```

## DESCRIPTION

**datapipe_read**() reads data from a data pipe. It may, depending on *flags*,
be used to discard data from a data pipe (without reading it), query the amount
of data available to be read, or peek at the data.

There are three mutually-exclusive mode flags that may be set in *flags*:
**MX_DATAPIPE_READ_FLAG_DISCARD**, **MX_DATAPIPE_READ_FLAG_QUERY**, and
**MX_DATAPIPE_READ_FLAG_PEEK**, which indicate the desired mode (none of these
flags being set means "read"). If **MX_DATAPIPE_READ_FLAG_ALL_OR_NONE** is set,
then either the entire requested operation is performed, or not at all.

In read mode, *requested* indicates the number of bytes (which must be a
multiple of the element size) to be read (to *buffer*, which should have room
for the requested number of bytes). On success, the number of bytes actually
read is returned. Unless **MX_DATAPIPE_READ_FLAG_ALL_OR_NONE** is set in
*flags*, this number may be less than *requested* (but will always be a multiple
of the element size).

In discard mode, *requested* indicates the number of bytes to discard and the
return value indicates the number of bytes actually discarded (with
**MX_DATAPIPE_READ_FLAG_ALL_OR_NONE** operating as above). *buffer* is ignored
in this case.

In query mode, both *requested* and *buffer* are ignored, as is the
**MX_DATAPIPE_READ_FLAG_ALL_OR_NONE** flag. The return value is the number of
bytes available to be read.

Peek mode operates like read mode, except that the data is not removed from the
data pipe.

## RETURN VALUE

**datapipe_read**() returns the number of bytes read/discarded/available/peeked
on success (see above). On failure, a (strictly) negative error value is
returned.

## ERRORS

**ERR_BAD_HANDLE**  *consumer_handle* is not a valid handle.

**ERR_WRONG_TYPE**  *consumer_handle* is not a handle to a data pipe producer.

**ERR_ACCESS_DENIED**  *consumer_handle* does not have **MX_RIGHT_READ**.

**ERR_NOT_SUPPORTED**  *flags* has an unknown flag set.

**ERR_ALREADY_BOUND**  *consumer_handle* is currently in a two-phase read.

**ERR_INVALID_ARGS**  *flags* has an invalid combination of flags set,
*requested* is not a multiple of the data pipe's element size (and query mode is
not specified), or *buffer* is an invalid pointer (or NULL).

**ERR_REMOTE_CLOSED**  (In read/discard/peek mode) The requested operation could
not be performed (due to a lack of data) and the remote data pipe producer
handle is closed.

**ERR_SHOULD_WAIT**  (In read/discard/peek mode) *requested* is nonzero and
**MX_DATAPIPE_READ_FLAG_ALL_OR_NONE** is not set, but the data pipe is empty
(and the producer is still open).

**ERR_OUT_OF_RANGE**  (In read/discard/peek mode) *requested* is nonzero and
**MX_DATAPIPE_READ_FLAG_ALL_OR_NONE** is set, but the data pipe does not have
the requested amount of data available (and the producer is still open).

## BUGS

The **ERR_OUT_OF_RANGE** will be changed to **ERR_SHOULD_WAIT** once read
thresholds are implemented (and a corresponding Mojo change is made).

## SEE ALSO

[datapipe_create](datapipe_create.md),
[datapipe_write](datapipe_write.md),
[datapipe_begin_write](datapipe_begin_write.md),
[datapipe_end_write](datapipe_end_write.md),
[datapipe_begin_read](datapipe_begin_read.md),
[datapipe_end_read](datapipe_end_read.md),
[handle_close](handle_close.md).
