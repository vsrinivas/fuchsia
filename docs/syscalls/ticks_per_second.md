# zx_ticks_per_second

## NAME

ticks_per_second - Read the number of high-precision timer ticks in a second.

## SYNOPSIS

```
#include <zircon/syscalls.h>

uint64_t zx_ticks_per_second(void)
```

## DESCRIPTION

**zx_ticks_per_second**() returns the number of high-precision timer ticks in a
second.

This can be used together with **zx_ticks_get**() to calculate the amount of
time elapsed between two subsequent calls to **zx_ticks_get**().

## RETURN VALUE

**zx_ticks_per_second**() returns the number of high-precision timer ticks in a
second.

## ERRORS

**zx_ticks_per_second**() does not report any error conditions.

## EXAMPLES

```
uint64_t ticks_per_second = zx_ticks_per_second();
uint64_t ticks_start = zx_ticks_get();

// do some more work

uint64_t ticks_end = zx_ticks_get();
double elapsed_seconds = (ticks_end - ticks_start) / (double)ticks_per_second;

```

## SEE ALSO

[ticks_get](ticks_get.md)
