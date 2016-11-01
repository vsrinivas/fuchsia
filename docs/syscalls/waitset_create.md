# mx_waitset_create

## NAME

waitset_create - create a wait set

## SYNOPSIS

```
#include <magenta/syscalls.h>

mx_status_t mx_waitset_create(uint32_t options, mx_handle_t* out);

```

## DESCRIPTION

**waitset_create**() creates a wait set, an object to which other handles can
be associated and which can be waited on, with the results of the wait depending
on those other handles.

A newly-created wait set handle has the **MX_RIGHT_READ** and **MX_RIGHT_WRITE**
rights. Note that it is neither duplicatable nor transferrable.

*options* must be zero.

## RETURN VALUE

**waitset_create**() returns NO_ERROR and a valid wait set handle via *out*.
On failure, an error value is returned.

## ERRORS

**ERR_INVALID_ARGS**  *out* is an invalid pointer.

**ERR_NO_MEMORY**  (Temporary) Failure due to lack of memory.

## SEE ALSO

[waitset_add](waitset_add.md),
[waitset_remove](waitset_remove.md),
[waitset_wait](waitset_wait.md),
[handle_close](handle_close.md).
