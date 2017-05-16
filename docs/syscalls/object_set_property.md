# mx_object_set_property

## NAME

object_set_property - Set various properties of various kernel objects.

## SYNOPSIS

```
#include <magenta/syscalls.h>

mx_status_t mx_object_set_property(mx_handle_t handle, uint32_t property,
                                   const void* value, size_t size);

```

## DESCRIPTION

See [object_get_property()](object_get_property.md) for a full description.
