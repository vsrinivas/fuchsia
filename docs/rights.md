# Rights

## Basics

Rights are associated with handles and convey privileges to perform actions on
either the associated handle or the object associated with the handle.

The [`<zircon/rights.h>`](../system/public/zircon/rights.h) header defines
default rights for each object type, which can be reduced via
`zx_handle_replace()` or `zx_handle_duplicate()`.

| Right | Conferred Privileges |
| ----- | -------------------- |
| **ZX_RIGHT_DUPLICATE** | Allows handle duplication via [*zx_handle_duplicate*](syscalls/handle_duplicate.md) |
| **ZX_RIGHT_TRANSFER** | Allows handle transfer via [*zx_channel_write*](syscalls/channel_write.md) |
| **ZX_RIGHT_READ** | Allows inspection of object state |
|                   | Allows reading of data from containers (channels, sockets, VM objects, etc) |
| **ZX_RIGHT_WRITE** | Allows modification of object state |
|                    | Allows writing of data to containers (channels, sockets, VM objects, etc) |
| **ZX_RIGHT_EXECUTE** | |
| **ZX_RIGHT_DEBUG** | Placeholder for debugger use, pending audit of all rights usage |

## See also
[Objects](objects.md),
[Zircon Handles](handles.md)
