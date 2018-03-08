# Rights

## Basics

Rights are associated with handles and convey privileges to perform actions on
either the associated handle or the object associated with the handle.

The [`<zircon/rights.h>`](../system/public/zircon/rights.h) header defines
default rights for each object type, which can be reduced via
`zx_handle_replace()` or `zx_handle_duplicate()`.

| Right | Conferred Privileges |
| ----- | -------------------- |
| **ZX_RIGHT_DUPLICATE**      | Allows handle duplication via [*zx_handle_duplicate*](syscalls/handle_duplicate.md) |
| **ZX_RIGHT_TRANSFER**       | Allows handle transfer via [*zx_channel_write*](syscalls/channel_write.md) |
| **ZX_RIGHT_READ**           | **TO BE REMOVED** Allows inspection of object state |
|                             | Allows reading of data from containers (channels, sockets, VM objects, etc) |
|                             | Allows mapping as readable if **ZX_RIGHT_MAP** is also present |
| **ZX_RIGHT_WRITE**          | **TO BE REMOVED** Allows modification of object state |
|                             | Allows writing of data to containers (channels, sockets, VM objects, etc) |
|                             | Allows mapping as writeable if **ZX_RIGHT_MAP** is also present |
| **ZX_RIGHT_EXECUTE**        | Allows mapping as executable if **ZX_RIGHT_MAP** is also present |
| **ZX_RIGHT_MAP**            | Allows mapping of a VM object into an address space. |
| **ZX_RIGHT_GET_PROPERTY**   | Allows property inspection via [*zx_object_get_property*](syscalls/object_get_property.md) |
| **ZX_RIGHT_SET_PROPERTY**   | Allows property modification via [*zx_object_set_property*](syscalls/object_set_property.md) |
| **ZX_RIGHT_ENUMERATE**      | Allows enumerating child objects via [*zx_object_get_info*](syscalls/object_get_info.md) and [*zx_object_get_child*](syscalls/object_get_child.md) |
| **ZX_RIGHT_DESTROY**        | Allows termination of task objects via [*zx_task_kill*](syscalls/task_kill.md)|
| **ZX_RIGHT_SET_POLICY**     | Allows policy modification via [*zx_job_set_policy*](syscalls/job_set_policy.md)|
| **ZX_RIGHT_GET_POLICY**     | Allows policy inspection via [*zx_job_get_policy*](syscalls/job_get_policy.md)|
| **ZX_RIGHT_SIGNAL**         | Allows use of [*zx_object_signal*](syscalls/object_signal.md) |
| **ZX_RIGHT_SIGNAL_PEER**    | Allows use of [*zx_object_signal_peer*](syscalls/object_signal.md) |
| **ZX_RIGHT_WAIT**           | Allows use of [*zx_object_wait_one*](syscalls/object_wait_one.md), [*zx_object_wait_many*](syscalls/object_wait_many.md), and other waiting primitives |
| **ZX_RIGHT_INSPECT**        | **NOT YET IMPLEMENTED** Allows inspection via [*zx_object_get_info*](syscalls/object_get_info.md) |
| **ZX_RIGHT_MANAGE_JOB**     | **NOT YET IMPLEMENTED** Allows creation of processes, subjobs, etc. |
| **ZX_RIGHT_MANAGE_PROCESS** | **NOT YET IMPLEMENTED** Allows creation of threads, etc |
| **ZX_RIGHT_MANAGE_THREAD**  | **NOT YET IMPLEMENTED** Allows suspending/resuming threads, etc|

## See also
[Objects](objects.md),
[Handles](handles.md)
