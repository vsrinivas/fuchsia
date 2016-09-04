# mx_handle_close

## NAME

handle_close - close a handle

## SYNOPSIS

```
#include <magenta/syscalls.h>

mx_status_t mx_handle_close(mx_handle_t handle);
```

## DESCRIPTION

**handle_close**() closes a *handle*, causing the underlying object to be
reclaimed by the kernel if no other handles to it exist.

## RETURN VALUE

**handle_close**() returns **NO_ERROR** on success.

## ERRORS

**ERR_BAD_HANDLE**  *handle* isn't a valid handle.

