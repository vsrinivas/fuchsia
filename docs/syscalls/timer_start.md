# mx_timer_start

## NAME

timer_start - start a timer  (deprecated)

## SYNOPSIS

```
#include <magenta/syscalls.h>

mx_status_t mx_timer_start(mx_handle_t handle, mx_time_t deadline, mx_duration_t period,
                           mx_duration_t slack);

```

## DESCRIPTION

This syscall is deprecated, use **mx_timer_set()**.

## SEE ALSO

[timer_create](timer_create.md),
[timer_set](timer_set.md),
[timer_cancel](timer_cancel.md),
[deadline_after](deadline_after.md)
