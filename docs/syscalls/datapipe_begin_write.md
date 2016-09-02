# mx_datapipe_begin_write

## NAME

datapipe_begin_write - begins a two-phase write to a data pipe

## SYNOPSIS

```
#include <magenta/syscalls.h>

mx_ssize_t mx_datapipe_begin_write(mx_handle_t producer_handle,
                                   uint32_t flags,
                                   uintptr_t* buffer);
```

## DESCRIPTION

TODO(vtl)

## RETURN VALUE

TODO(vtl)

## ERRORS

TODO(vtl)

## SEE ALSO

[datapipe_create](datapipe_create.md),
[datapipe_write](datapipe_write.md),
[datapipe_end_write](datapipe_end_write.md),
[datapipe_read](datapipe_read.md),
[datapipe_begin_read](datapipe_begin_read.md),
[datapipe_end_read](datapipe_end_read.md),
[handle_close](handle_close.md).

