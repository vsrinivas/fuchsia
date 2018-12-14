# zx_system_get_physmem

## NAME

<!-- Updated by update-docs-from-abigen, do not edit. -->

system_get_physmem - get amount of physical memory on the system

## SYNOPSIS

<!-- Updated by update-docs-from-abigen, do not edit. -->

```
#include <zircon/syscalls.h>

uint64_t zx_system_get_physmem(void);
```

## DESCRIPTION

`zx_system_get_physmem()` returns the total size of physical memory on
the machine, in bytes.

## RIGHTS

<!-- Updated by update-docs-from-abigen, do not edit. -->

TODO(ZX-2399)

## RETURN VALUE

`zx_system_get_physmem()` returns a number in bytes.

## ERRORS

`zx_system_get_physmem()` cannot fail.

## NOTES

Currently the total size of physical memory cannot change during a run of
the system, only at boot time.  This might change in the future.

## SEE ALSO


[system_get_num_cpus](system_get_num_cpus.md).
