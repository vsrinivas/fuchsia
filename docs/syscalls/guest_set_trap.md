# mx_guest_set_trap

## NAME

guest_set_trap - sets a trap within a guest

## SYNOPSIS

```
#include <magenta/syscalls.h>
#include <magenta/syscalls/port.h>

mx_status_t mx_guest_set_trap(mx_handle_t guest, uint32_t kind, mx_vaddr_t addr,
                              size_t len, mx_handle_t port, uint64_t key);
```

## DESCRIPTION

**guest_set_trap**() sets a trap within a guest, which generates a packet when
there is an access by a VCPU within the address range defined by *addr* and
*len*, within the address space defined by *kind*.

If *port* is specified, a packet with a *key* for the trap will be delivered
through the port each time it is triggered, otherwise if *MX_HANDLE_INVALID* is
given, the packet will be delivered through **vcpu_resume**() and a key of 0
will be set. This provides control over whether the packet is delivered
asynchronously or synchronously, and provides the ability to distinguish packets
multiplexed onto the same port.

When *port* is specified, a fixed number of packets are pre-allocated per trap.
If all the packets are exhausted, execution of the VCPU that caused the trap
will be paused. When at least one packet is dequeued, execution of the VCPU will
resume. To dequeue a packet from *port*, use *port_wait*(). Multiple threads may
use *port_wait*() to dequeue packets, enabling the use of a thread pool to
handle traps.

*kind* may be either *MX_GUEST_TRAP_MEM* or *MX_GUEST_TRAP_IO*. If
*MX_GUEST_TRAP_MEM* is specified, then *addr* and *len* must both be
page-aligned.

To identify what *kind* of trap generated a packet, use *MX_PKT_TYPE_GUEST_MEM*
and *MX_PKT_TYPE_GUEST_IO*.

## RETURN VALUE

**guest_set_trap**() returns MX_OK on success. On failure, an error value is
returned.

## ERRORS

**MX_ERR_ACCESS_DENIED** *guest* or *port* do not have the *MX_RIGHT_WRITE*
right.

**MX_ERR_BAD_HANDLE** *guest* or *port* are invalid handles.

**MX_ERR_INVALID_ARGS** *kind* is not a valid address space, *addr* or *len*
do not meet the requirements of *kind*, or *len* is 0.

**MX_ERR_NO_MEMORY** Temporary failure due to lack of memory.

**MX_ERR_OUT_OF_RANGE** The region specified by *addr* and *len* is outside of
of the valid bounds of the address space *kind*.

**MX_ERR_WRONG_TYPE** *guest* is not a handle to a guest, or *port* is not a
handle to a port.

## SEE ALSO

[guest_create](guest_create.md),
[port_create](port_create.md),
[port_wait](port_wait.md),
[vcpu_create](vcpu_create.md),
[vcpu_resume](vcpu_resume.md),
[vcpu_interrupt](vcpu_interrupt.md),
[vcpu_read_state](vcpu_read_state.md),
[vcpu_write_state](vcpu_write_state.md).
