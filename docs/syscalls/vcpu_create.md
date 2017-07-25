# mx_vcpu_create

## NAME

vcpu_create - create a VCPU

## SYNOPSIS

```
#include <magenta/syscalls.h>
#include <magenta/syscalls/hypervisor.h>

mx_status_t mx_vcpu_create(mx_handle_t guest, uint32_t options,
                           const mx_vcpu_create_args_t* args, mx_handle_t* out);
```

## DESCRIPTION

**vcpu_create**() creates a VCPU within a guest, which allows for execution
within the virtual machine. One or more VCPUs may be created per guest, where
the number of VCPUs does not need to match the number of physical CPUs on the
machine.

*args* contains the instruction pointer used to indicate where in guest physical
memory execution of the VCPU should start, as well as architecture-specific
arguments required to begin execution.

*vcpu* is bound to the thread that created it, and all syscalls that operate on
it must be called from the same thread, with the exception of
**vcpu_interrupt**().

N.B. VCPU is an abbreviation of virtual CPU.

The following rights will be set on the handle *out* by default:

**MX_RIGHT_DUPLICATE** — *out* may be duplicated.

**MX_RIGHT_TRANSFER** — *out* may be transferred over a channel.

**MX_RIGHT_EXECUTE** — *out* may have its execution resumed (or begun)

**MX_RIGHT_SIGNAL** — *out* may be interrupted

**MX_RIGHT_READ** — *out* may have its state read

**MX_RIGHT_WRITE** — *out* may have its state written

## RETURN VALUE

**vcpu_create**() returns MX_OK on success. On failure, an error value is
returned.

## ERRORS

**MX_ERR_ACCESS_DENIED** *guest* does not have the *MX_RIGHT_WRITE* right, or
*apic_vmo* does not have the *MX_RIGHT_READ* and *MX_RIGHT_WRITE* rights.

**MX_ERR_BAD_HANDLE** *guest* is an invalid handle.

**MX_ERR_INVALID_ARGS** *args* contains an invalid argument, or *out* is an
invalid pointer, or *options* is nonzero.

**MX_ERR_NO_MEMORY** Temporary failure due to lack of memory.

**MX_ERR_WRONG_TYPE** *guest* is not a handle to a guest.

## SEE ALSO

[guest_create](guest_create.md),
[guest_set_trap](guest_set_trap.md),
[vcpu_resume](vcpu_resume.md),
[vcpu_interrupt](vcpu_interrupt.md),
[vcpu_read_state](vcpu_read_state.md),
[vcpu_write_state](vcpu_write_state.md).
