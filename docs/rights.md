# Rights

## Basics

Rights are associated with handles and convey privileges to perform actions on
either the associated handle or the object associated with the handle.

| Right | Conferred Privileges |
| ----- | -------------------- |
| **MX_RIGHT_DUPLICATE** | Allows handle duplication via [*mx_handle_duplicate*](syscalls/handle_duplicate.md) |
| **MX_RIGHT_TRANSFER** | Allows handle transfer via [*mx_message_write*](syscalls/message_write.md) |
| **MX_RIGHT_READ** | Allows inspection of object state |
|                   | Allows reading of data from containers (pipes, VM objects, etc) |
| **MX_RIGHT_WRITE** | Allows modification of object state |
|                    | Allows writing of data to containers (pipes, VM objects, etc) |
| **MX_RIGHT_EXECUTE** | |
| **MX_RIGHT_DEBUG** | Placeholder for debugger use, pending audit of all rights usage |

## See also
[Kernel Objects](kernel_objects.md),
[Magenta Handles](handles.md)
