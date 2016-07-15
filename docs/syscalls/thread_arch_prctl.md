# mx_thread_arch_prctl

## NAME

thread_arch_prctl - Manipulate architecture specific registers.

## SYNOPSIS

```
#include <magenta/prctl.h>
#include <magenta/syscalls.h>

mx_status_t mx_futex_wait(mx_handle_t handle_value, uint32_t op, uintptr_t* value_ptr);
```

## DESCRIPTION

The **magenta/prctl.h** header defines a set of valid values for
**op** for each architecture. The following operations are currently
supported:

### arm

```
ARCH_SET_CP15_READONLY // Set the userspace-readonly cp15 register to *value_ptr.
```

### aarch64

```
ARCH_SET_TPIDRRO_EL0 // Set the userspace-readonly tpidrro_el0 register to *value_ptr.
```

### x86_64

```
ARCH_SET_FS // Set the userspace-readonly fs register to from value_ptr.
ARCH_GET_FS // Read the userspace-readonly fs register into value_ptr.
ARCH_SET_GS // Set the userspace-readonly gs register to from value_ptr.
ARCH_GET_GS // Read the userspace-readonly gs register into value_ptr.
```

## RETURN VALUE

**thread_arch_prctl**() returns **NO_ERROR** on success.

## ERRORS

**ERR_INVALID_ARGS**  *value_ptr* is not a valid userspace pointer.

**ERR_INVALID_ARGS**  The value at *value_ptr* is not valid for the chosen operation.

**ERR_INVALID_ARGS**  *op* is not a valid operation for the current architecture.
