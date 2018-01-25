# zx_system_get_cpu_features

## NAME

system_get_cpu_features - get supported processor capabilities

## SYNOPSIS

```
#include <zircon/syscalls.h>

zx_status_t zx_system_cpu_features(uint32_t* features);
```

## DESCRIPTION

**system_get_cpu_features**() populates *features* with a bit mask of
processor-specific features.  The individual feature represented by each bit is
hardware-dependent.  If an architectures provides a different, preferred way to
get this information, e.g. via **__get_cpuid** on x86-64, an error will be
returned.

## RETURN VALUE

**system_get_cpu_features**() returns the hardware-dependent bit mask.

## ERRORS

**system_get_cpu_features**()  returns **ZX_OK** on success.

**ZX_ERR_NOT_SUPPORTED**  The architecture provides another way to get
processor capabilities that should be preferred, e.g. **__get_cpuid** on x86-64.

## NOTES
Refer to [Architecture Support](../architecture_support.md) for supported
processor architectures.

For x86-64, use **__get_cpuid** from cpuid.h instead of this function.

## SEE ALSO
[system_get_num_cpus](system_get_num_cpus.md)
[system_get_physmem](system_get_physmem.md)
