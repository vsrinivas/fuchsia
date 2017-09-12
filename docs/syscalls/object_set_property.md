# zx_object_set_property

## NAME

object_set_property - Set various properties of various kernel objects.

## SYNOPSIS

```
#include <zircon/syscalls.h>

zx_status_t zx_object_set_property(zx_handle_t handle, uint32_t property,
                                   const void* value, size_t size);

```

## DESCRIPTION

See [object_get_property()](object_get_property.md) for a full description.
