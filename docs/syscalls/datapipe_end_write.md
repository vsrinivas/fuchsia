# mx_datapipe_end_write

## NAME

datapipe_end_write - ends a two-phase write to a data pipe

## SYNOPSIS

```
#include <magenta/syscalls.h>

mx_status_t mx_datapipe_end_write(mx_handle_t producer_handle,
                                  mx_size_t written);
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
[datapipe_begin_write](datapipe_begin_write.md),
[datapipe_read](datapipe_read.md),
[datapipe_begin_read](datapipe_begin_read.md),
[datapipe_end_read](datapipe_end_read.md),
[handle_close](handle_close.md).

