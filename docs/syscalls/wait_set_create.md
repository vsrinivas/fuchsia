# mx_wait_set_create

## NAME

wait_set_create - create a wait set

## SYNOPSIS

```
#include <magenta/syscalls.h>

mx_status_t mx_wait_set_create(void);

```

## DESCRIPTION

**wait_set_create**() creates a wait set, an object to which other handles can
be associated and which can be waited on, with the results of the wait depending
on those other handles.

A newly-created wait set handle has the **MX_RIGHT_READ** and **MX_RIGHT_WRITE**
rights. Note that it is neither duplicatable nor transferrable.

## RETURN VALUE

**wait_set_create**() returns a valid wait set handle (strictly positive) on
success. On failure, a (strictly) negative error value is returned. Zero (the
"invalid handle") is never returned.

## ERRORS

**ERR_NO_MEMORY**  (Temporary) Failure due to lack of memory.

## SEE ALSO

[wait_set_add](wait_set_add.md),
[wait_set_remove](wait_set_remove.md),
[wait_set_wait](wait_set_wait.md),
[handle_close](handle_close.md).
