# mx_ticks_per_second

## NAME

ticks_per_second - Read the number of high-precision timer ticks in a second.

## SYNOPSIS

```
#include <magenta/syscalls.h>

uint64_t mx_ticks_per_second(void)
```

## DESCRIPTION

**mx_ticks_per_second**() returns the number of high-precision timer ticks in a
second.

This can be used together with **mx_ticks_get**() to calculate the amount of
time elapsed between two subsequent calls to **mx_ticks_get**().

## RETURN VALUE

**mx_ticks_per_second**() returns the number of high-precision timer ticks in a
second.

## ERRORS

**mx_ticks_per_second**() does not report any error conditions.

## EXAMPLES

```
uint64_t ticks_per_second = mx_ticks_per_second();
uint64_t ticks_start = mx_ticks_get();

// do some more work

uint64_t ticks_end = mx_ticks_get();
double elapsed_seconds = (ticks_end - ticks_start) / (double)ticks_per_second;

```

## SEE ALSO

[ticks_get](ticks_get.md)
