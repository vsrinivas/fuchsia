# mx_guest_set_trap

## NAME

guest_set_trap - sets a trap within a guest

## SYNOPSIS

```
#include <magenta/syscalls.h>
#include <magenta/syscalls/hypervisor.h>

mx_status_t mx_guest_set_trap(mx_handle_t guest, uint32_t kind, mx_vaddr_t addr,
                              size_t len, mx_handle_t fifo);
```

## DESCRIPTION

**guest_set_trap**() sets a trap within a guest, which generates a packet when
when there is an access by a VCPU within the address range defined by *addr* and
*len*, within the address space defined by *kind*.

If *fifo* is specified, a *mx_guest_packet_t* packet for the trap will be
delivered through the FIFO, otherwise if *MX_HANDLE_INVALID* is given, the
packet will be delivered through **vcpu_resume**(). This provides control over
whether the packet is delivered asynchronously, or synchronously.

If *fifo* is full, execution of the VCPU that caused the trap will be paused.
When the FIFO is no longer full, execution of the VCPU will resume.

When *fifo* is created, its *elem_size* must be equivalent to
*sizeof(mx_guest_packet_t)*.

*kind* may be either *MX_GUEST_TRAP_MEMORY* or *MX_GUEST_TRAP_IO*. If
*MX_GUEST_TRAP_MEMORY* is specified, then *addr* and *len* must both be
page-aligned.

## RETURN VALUE

**guest_set_trap**() returns MX_OK on success. On failure, an error value is
returned.

## ERRORS

**MX_ERR_ACCESS_DENIED** *guest* or *fifo* do not have the *MX_RIGHT_WRITE*
right.

**MX_ERR_BAD_HANDLE** *guest* or *fifo* are invalid handles.

**MX_ERR_INVALID_ARGS** *kind* is not a valid address space, or *addr* or
*len* does not meet the requirements of *kind*.

**MX_ERR_NO_MEMORY** Temporary failure due to lack of memory.

**MX_ERR_OUT_OF_RANGE** The region specified by *addr* and *len* is outside of
of the valid bounds of the address space *kind*.

**MX_ERR_WRONG_TYPE** *guest* is not a handle to a guest, or *fifo* is not a
handle to a FIFO.

## SEE ALSO

[fifo_create](fifo_create.md),
[guest_create](guest_create.md),
[vcpu_create](vcpu_create.md),
[vcpu_resume](vcpu_resume.md),
[vcpu_interrupt](vcpu_interrupt.md),
[vcpu_read_state](vcpu_read_state.md),
[vcpu_write_state](vcpu_write_state.md).
