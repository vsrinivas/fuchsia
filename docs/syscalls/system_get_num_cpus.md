# zx_system_num_cpus

## NAME

system_get_num_cpus - get number of logical processors on the system

## SYNOPSIS

```
#include <zircon/syscalls.h>

uint32_t zx_system_get_num_cpus(void);
```

## DESCRIPTION

**system_get_num_cpus**() returns the number of CPUs (logical processors)
that exist on the system currently running.  This number cannot change
during a run of the system, only at boot time.

## RIGHTS

TODO(ZX-2399)

## RETURN VALUE

**system_get_num_cpus**() returns the number of CPUs.

## ERRORS

**system_get_num_cpus**() cannot fail.

## NOTES

## SEE ALSO
[system_get_physmem](system_get_physmem.md).
