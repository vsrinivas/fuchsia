# Rights

## Basics

Rights are associated with handles and convey privileges to perform actions on
either the associated handle or the object associated with the handle.

The [`<magenta/rights.h>`](../system/public/magenta/rights.h) header defines
default rights for each object type, which can be reduced via
`mx_handle_replace()` or `mx_handle_duplicate()`.

| Right | Conferred Privileges |
| ----- | -------------------- |
| **MX_RIGHT_DUPLICATE** | Allows handle duplication via [*mx_handle_duplicate*](syscalls/handle_duplicate.md) |
| **MX_RIGHT_TRANSFER** | Allows handle transfer via [*mx_channel_write*](syscalls/channel_write.md) |
| **MX_RIGHT_READ** | Allows inspection of object state |
|                   | Allows reading of data from containers (channels, sockets, VM objects, etc) |
| **MX_RIGHT_WRITE** | Allows modification of object state |
|                    | Allows writing of data to containers (channels, sockets, VM objects, etc) |
| **MX_RIGHT_EXECUTE** | |
| **MX_RIGHT_DEBUG** | Placeholder for debugger use, pending audit of all rights usage |

## See also
[Objects](objects.md),
[Magenta Handles](handles.md)
