# mx_datapipe_create

## NAME

datapipe_create - create a data pipe

## SYNOPSIS

```
#include <magenta/syscalls.h>

mx_handle_t mx_datapipe_create(uint32_t options,
                               mx_size_t element_size,
                               mx_size_t capacity,
                               mx_handle_t* consumer_handle);
```

## DESCRIPTION

**datapipe_create**() creates a data pipe, a unidirectional communication
channel for unframed data.

A data pipe operates on "elements" of size *element_size* (which must be at
least 1) in bytes. (Data must always be provided and consumed in multiples of
*element_size*. E.g., an element may not be partially consumed.)

*capacity* is the requested capacity of the data pipe in bytes, and must be a
multiple of *element_size*. The actual capacity of the data pipe will always be
at least this, but may be more. Specifying 0 for *capacity* indicates to the
system that the data pipe will be of some default size.

On success, the producer handle (which will have the **MX_RIGHT_TRANSFER**,
**MX_RIGHT_WRITE**, and **MX_RIGHT_READ** rights) is returned and the consumer
handle (which will have the **MX_RIGHT_TRANSFER** and **MX_RIGHT_READ** rights)
is provided via *consumer_handle*.

## RETURN VALUE

**datapipe_create**() returns a valid data pipe producer handle (strictly
positive) on success. On failure, a (strictly) negative error value is returned.
Zero (the "invalid handle") is never returned.

## ERRORS

**ERR_INVALID_ARGS**  *element_size* is 0, *capacity* is not a multiple of
*element_size*, or *consumer_handle* is an invalid pointer (or NULL).

**ERR_NO_MEMORY**  Temporary failure due to lack of memory or if *capacity* is
too large.

## SEE ALSO

[datapipe_write](datapipe_write.md),
[datapipe_begin_write](datapipe_begin_write.md),
[datapipe_end_write](datapipe_end_write.md),
[datapipe_read](datapipe_read.md),
[datapipe_begin_read](datapipe_begin_read.md),
[datapipe_end_read](datapipe_end_read.md),
[handle_close](handle_close.md).
