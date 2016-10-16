# mx_current_time

## NAME

current_time - Acquire the current time.

## SYNOPSIS

```
#include <magenta/syscalls.h>

mx_time_t mx_current_time(void);
```

## DESCRIPTION

**mx_current_time**() returns the number of nanoseconds since the system was
powered on.

## RETURN VALUE

**mx_current_time**() returns nanoseconds, and cannot return an error.

## ERRORS

## BUGS

