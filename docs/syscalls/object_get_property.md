# zx_object_get_property, zx_object_set_property

## NAME

object_get_property - Ask for various properties of various kernel objects.

object_set_property - Set various properties of various kernel objects.

## SYNOPSIS

```
#include <zircon/syscalls.h>
#include <zircon/syscalls/object.h>

zx_status_t zx_object_get_property(zx_handle_t handle, uint32_t property,
                                   void* value, size_t value_size);

zx_status_t zx_object_set_property(zx_handle_t handle, uint32_t property,
                                   const void* value, size_t value_size);
```

## DESCRIPTION

**zx_object_get_property()** requests the value of a kernel object's property.
Getting a property requires **ZX_RIGHT_GET_PROPERTY** rights on the handle.

**zx_object_set_property()** modifies the value of a kernel object's property.
Setting a property requires **ZX_RIGHT_SET_PROPERTY** rights on the handle.

The *handle* parameter indicates the target kernel object. Different properties
only work on certain types of kernel objects, as described below.

The *property* parameter indicates which property to get/set. Property values
have the prefix **ZX_PROP_**, and are described below.

The *value* parameter holds the property value, and must be a pointer to a
buffer of *size* bytes. Different properties expect different value types/sizes
as described below.

## PROPERTIES

Property values have the prefix **ZX_PROP_**, and are defined in

```
#include <zircon/syscalls/object.h>
```

### ZX_PROP_NAME

*handle* type: **(Most types)**

*value* type: **char\[ZX_MAX_NAME_LEN\]**

Allowed operations: **get**, **set**

The name of the object, as a NUL-terminated string.

### ZX_PROP_REGISTER_FS and ZX_PROP_REGISTER_GS

*handle* type: **Thread**

*value* type: **uintptr_t**

Allowed operations: **set**

The value of the x86 FS or GS segment register. `value` must be a
canonical address, and must be a userspace address.

Only defined for x86-64.

### ZX_PROP_PROCESS_DEBUG_ADDR

*handle* type: **Process**

*value* type: **uintptr_t**

Allowed operations: **get**, **set**

The value of ld.so's `_dl_debug_addr`. This can be used by debuggers to
interrogate the state of the dynamic loader.

If this value is set to `ZX_PROCESS_DEBUG_ADDR_BREAK_ON_SET` on process
creation, the loader will manually issue a debug breakpoint when the property
has been set to its correct value. This gives an opportunity to read or modify
the initial state of the program.

### ZX_PROP_PROCESS_VDSO_BASE_ADDRESS

*handle* type: **Process**

*value* type: **uintptr_t**

Allowed operations: **get**

The base address of the vDSO mapping, or zero.

### ZX_PROP_JOB_IMPORTANCE

*handle* type: **Job**

*value* type: **zx_job_importance_t**

Allowed operations: **get**, **set**

A hint about how important a job is; used to rank jobs for the out-of-memory
(OOM) killer.

Additional errors:

*   **ZX_ERR_OUT_OF_RANGE**: If the importance value is not valid

### ZX_PROP_SOCKET_RX_BUF_MAX

*handle* type: **Socket**

*value* type: **size_t**

Allowed operations: **get**

The maximum size of the receive buffer of a socket, in bytes. The receive
buffer may become full at a capacity less than the maximum due to overheads.

### ZX_PROP_SOCKET_RX_BUF_SIZE

*handle* type: **Socket**

*value* type: **size_t**

Allowed operations: **get**

The size of the receive buffer of a socket, in bytes.

### ZX_PROP_SOCKET_TX_BUF_MAX

*handle* type: **Socket**

*value* type: **size_t**

Allowed operations: **get**

The maximum size of the transmit buffer of a socket, in bytes. The transmit
buffer may become full at a capacity less than the maximum due to overheads.

### ZX_PROP_SOCKET_TX_BUF_SIZE

*handle* type: **Socket**

*value* type: **size_t**

Allowed operations: **get**

The size of the transmit buffer of a socket, in bytes.

### ZX_PROP_CHANNEL_TX_MSG_MAX

*handle* type: **Channel**

*value* type: **size_t**

Allowed operations: **get**

The maximum number of packets a channel endpoint can have pending in
its outgoing direction.

## RIGHTS

TODO(ZX-2399)

## RETURN VALUE

**zx_object_get_property**() returns **ZX_OK** on success. In the event of
failure, a negative error value is returned.

## ERRORS

**ZX_ERR_BAD_HANDLE**: *handle* is not a valid handle

**ZX_ERR_WRONG_TYPE**: *handle* is not an appropriate type for *property*

**ZX_ERR_ACCESS_DENIED**: *handle* does not have the necessary rights for the
operation

**ZX_ERR_INVALID_ARGS**: *value* is an invalid pointer

**ZX_ERR_NO_MEMORY**  Failure due to lack of memory.
There is no good way for userspace to handle this (unlikely) error.
In a future build this error will no longer occur.

**ZX_ERR_BUFFER_TOO_SMALL**: *value_size* is too small for *property*

**ZX_ERR_NOT_SUPPORTED**: *property* does not exist

## SEE ALSO

[object_set_property](object_set_property.md)
