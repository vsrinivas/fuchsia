# zx_system_get_features

## NAME

system_get_features - get supported hardware capabilities

## SYNOPSIS

```
#include <zircon/syscalls.h>

zx_status_t zx_system_features(uint32_t kind, uint32_t* features);
```

## DESCRIPTION

**system_get_features**() populates *features* with a bit mask of
hardware-specific features.  *kind* indicates the specific type of features
to retrieve, e.g. *ZX_FEATURE_KIND_CPU*.  The supported kinds and the meaning
of individual feature bits is hardware-dependent.

## RIGHTS

TODO(ZX-2399)

## RETURN VALUE

**system_get_features**()  returns **ZX_OK** on success.

## ERRORS

**ZX_ERR_NOT_SUPPORTED**  The requested feature kind is not available on this
platform.

## NOTES
Refer to [Architecture Support](../architecture_support.md) for supported
processor architectures.

Refer to [zircon/features.h](../../system/public/zircon/features.h) for kinds
of features and individual feature bits.

## SEE ALSO
[system_get_num_cpus](system_get_num_cpus.md)
[system_get_physmem](system_get_physmem.md)
