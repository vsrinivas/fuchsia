# mx_datapipe_begin_read

## NAME

datapipe_begin_read - begins a two-phase read from a data pipe

## SYNOPSIS

```
#include <magenta/syscalls.h>

mx_ssize_t mx_datapipe_begin_read(mx_handle_t consumer_handle,
                                  uint32_t flags,
                                  uintptr_t* buffer);
```

## DESCRIPTION

**datapipe_begin_read**() begins a two-phase read from a data pipe. That is, it
first ensures that (some of) the data available to be read is accessible in the
caller's address space, e.g., possibly by mapping memory, and then it provides
that data's address in *buffer*. The amount of data available to be read
starting from that address is returned (on success). (This address may not be
written to; doing so may cause a fault.) Note that this amount may be strictly
less than the total amount available to be read from the data pipe (e.g., if the
data is not contiguous).

There are currently no supported flags, so *flags* must be set to 0.

After the caller has finished reading the data, the two-phase read should be
ended by calling **datapipe_end_read**(), which mark data as consumed.

## RETURN VALUE

**datapipe_begin_read**() returns a (strictly) positive number indicating the
amount of data available to be read starting at the address provided in *buffer*
on success. On failure, a (strictly) negative error value is returned.

## ERRORS

**ERR_BAD_HANDLE**  *consumer_handle* is not a valid handle.

**ERR_WRONG_TYPE**  *consumer_handle* is not a handle to a data pipe producer.

**ERR_ACCESS_DENIED**  *consumer_handle* does not have **MX_RIGHT_READ**.

**ERR_NOT_SUPPORTED**  *flags* has an unknown flag set.

**ERR_ALREADY_BOUND**  *consumer_handle* is currently in a two-phase read.

**ERR_INVALID_ARGS**  *flags* has a known flag set, or *buffer* is an invalid
pointer (or NULL).

**ERR_REMOTE_CLOSED**  The data pipe is empty and the producer is closed.

**ERR_SHOULD_WAIT**  The data pipe is empty and the producer is still open.

**ERR_NO_MEMORY**  Temporary failure due to lack of memory or address space.

## SEE ALSO

[datapipe_create](datapipe_create.md),
[datapipe_write](datapipe_write.md),
[datapipe_begin_write](datapipe_begin_write.md),
[datapipe_end_write](datapipe_end_write.md),
[datapipe_read](datapipe_read.md),
[datapipe_end_read](datapipe_end_read.md),
[handle_close](handle_close.md).
