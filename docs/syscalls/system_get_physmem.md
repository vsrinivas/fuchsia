# mx_system_get_physmem

## NAME

system_get_physmem - get amount of physical memory on the system

## SYNOPSIS

```
#include <magenta/syscalls.h>

size_t mx_system_get_physmem(void);
```

## DESCRIPTION

**system_get_physmem**() returns the total size of physical memory on
the machine, in bytes.

## RETURN VALUE

**system_get_physmem**() returns a number in bytes.

## ERRORS

**system_get_physmem**() cannot fail.

## NOTES

Currently the total size of physical memory cannot change during a run of
the system, only at boot time.  This might change in the future.

## SEE ALSO
[system_get_num_cpus](system_get_num_cpus.md).
