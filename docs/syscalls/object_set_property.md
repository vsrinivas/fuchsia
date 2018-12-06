# zx_object_set_property

## NAME

<!-- Updated by scripts/update-docs-from-abigen, do not edit this section manually. -->

object_set_property - Set various properties of various kernel objects.

## SYNOPSIS

```
#include <zircon/syscalls.h>

zx_status_t zx_object_set_property(zx_handle_t handle, uint32_t property,
                                   const void* value, size_t value_size);

```

## DESCRIPTION

See [object_get_property()](object_get_property.md) for a full description.

## RIGHTS

<!-- Updated by scripts/update-docs-from-abigen, do not edit this section manually. -->

*handle* must have **ZX_RIGHT_SET_PROPERTY**.

If *property* is **ZX_PROP_PROCESS_DEBUG_ADDR**, *handle* must be of type **ZX_OBJ_TYPE_PROCESS**.

If *property* is **ZX_PROP_SOCKET_RX_THRESHOLD**, *handle* must be of type **ZX_OBJ_TYPE_SOCKET**.

If *property* is **ZX_PROP_SOCKET_TX_THRESHOLD**, *handle* must be of type **ZX_OBJ_TYPE_SOCKET**.

If *property* is **ZX_PROP_JOB_KILL_ON_OOM**, *handle* must be of type **ZX_OBJ_TYPE_JOB**.

## SEE ALSO

[object_get_property()](object_get_property.md).
