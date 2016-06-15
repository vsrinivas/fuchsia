# _magenta_handle_duplicate

## NAME

handle_duplicate - duplicate a handle

## SYNOPSIS

```
#include <magenta/syscalls.h>

mx_handle_t _magenta_handle_duplicate(mx_handle_t handle);
```

## DESCRIPTION

**handle_duplicate**() creates a duplicate of *handle*, referring
to the same underlying object, suitable for passing to another process
via a message pipe.

## RETURN VALUE

**handle_duplicate**() returns the duplicate handle on success (a
positive value), or an error code (negative).

## ERRORS

**ERR_INVALID_ARGS**  *handle* isn't a valid handle.

**ERR_ACCESS_DENIED**  *handle* does not have **MX_RIGHT_DUPLICATE** and may not be duplicated.

**ERR_NO_MEMORY**  (Temporary) out of memory situation.

