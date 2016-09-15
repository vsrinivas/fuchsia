# mx_datapipe_end_read

## NAME

datapipe_end_read - ends a two-phase read from a data pipe

## SYNOPSIS

```
#include <magenta/syscalls.h>

mx_status_t mx_datapipe_end_read(mx_handle_t consumer_handle, mx_size_t read);
```

## DESCRIPTION

**datapipe_end_read**() ends a two-phase read from a data pipe, which should
have been started using **datapipe_begin_read**(). That is, it marks *read*
bytes as consumed (freeing space for the producer and making it no longer
available to the consumer). It may also, e.g., unmap memory from the caller's
address space.

*read* must be at most the value returned by the preceding call to
**datapipe_begin_read**() (it may be 0). After calling **datapipe_end_read**(),
the caller may no longer read from the buffer provided by
**datapipe_begin_read**().

## RETURN VALUE

**datapipe_end_read**() returns **NO_ERROR** on success, or a (strictly)
negative error value on failure.

## ERRORS

**ERR_BAD_HANDLE**  *consumer_handle* is not a valid handle.

**ERR_WRONG_TYPE**  *consumer_handle* is not a handle to a data pipe producer.

**ERR_ACCESS_DENIED**  *consumer_handle* does not have **MX_RIGHT_READ**.

**ERR_BAD_STATE**  *consumer_handle* is currently not in a two-phase read (i.e.,
there is no preceding call to **datapipe_begin_read**() not "terminated" by a
corresponding call to **datapipe_end_read**()).

**ERR_INVALID_ARGS**  *read* is larger than the value returned by the preceding
call to **datapipe_begin_read**() or is not a multiple of the data pipe's
element size.

## SEE ALSO

[datapipe_create](datapipe_create.md),
[datapipe_write](datapipe_write.md),
[datapipe_begin_write](datapipe_begin_write.md),
[datapipe_end_write](datapipe_end_write.md),
[datapipe_read](datapipe_read.md),
[datapipe_begin_read](datapipe_begin_read.md),
[handle_close](handle_close.md).
