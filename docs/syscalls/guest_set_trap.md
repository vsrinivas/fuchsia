# zx_guest_set_trap

## NAME

guest_set_trap - sets a trap within a guest

## SYNOPSIS

```
#include <zircon/syscalls.h>
#include <zircon/syscalls/port.h>

zx_status_t zx_guest_set_trap(zx_handle_t guest, uint32_t kind, zx_vaddr_t addr,
                              size_t len, zx_handle_t port, uint64_t key);
```

## DESCRIPTION

**guest_set_trap**() sets a trap within a guest, which generates a packet when
there is an access by a VCPU within the address range defined by *addr* and
*len*, within the address space defined by *kind*.

If *port* is specified, a packet for the trap will be delivered through *port*
each time the trap is triggered, otherwise if *ZX_HANDLE_INVALID* is given, a
packet will be delivered through **vcpu_resume**(). This provides control over
whether the packet is delivered asynchronously or synchronously.

When *port* is specified, a fixed number of packets are pre-allocated per trap.
If all the packets are exhausted, execution of the VCPU that caused the trap
will be paused. When at least one packet is dequeued, execution of the VCPU will
resume. To dequeue a packet from *port*, use **port_wait**(). Multiple threads
may use **port_wait**() to dequeue packets, enabling the use of a thread pool to
handle traps.

*key* is used to set the key field within *zx_port_packet_t*, and can be used to
distinguish between packets for different traps.

*kind* may be either *ZX_GUEST_TRAP_BELL*, *ZX_GUEST_TRAP_MEM*, or
*ZX_GUEST_TRAP_IO*. If *ZX_GUEST_TRAP_BELL* or *ZX_GUEST_TRAP_MEM* is specified,
then *addr* and *len* must both be page-aligned. If *ZX_GUEST_TRAP_BELL* is set,
then *port* must be specified. If *ZX_GUEST_TRAP_MEM* or *ZX_GUEST_TRAP_IO* is
set, then *port* must be *ZX_HANDLE_INVALID*.

*ZX_GUEST_TRAP_BELL* is a type of trap that defines a door-bell. If there is an
access to the memory region specified by the trap, then a packet is generated
that does not fetch the instruction associated with the access. The packet will
then be delivered via *port*.

To identify what *kind* of trap generated a packet, use *ZX_PKT_TYPE_GUEST_MEM*,
*ZX_PKT_TYPE_GUEST_IO*, *ZX_PKT_TYPE_GUEST_BELL*, and *ZX_PKT_TYPE_GUEST_VCPU*.
*ZX_PKT_TYPE_GUEST_VCPU* is a special packet, not caused by a trap, that
indicates that the guest requested to start an additional VCPU.

## RETURN VALUE

**guest_set_trap**() returns ZX_OK on success. On failure, an error value is
returned.

## ERRORS

**ZX_ERR_ACCESS_DENIED** *guest* or *port* do not have the *ZX_RIGHT_WRITE*
right.

**ZX_ERR_ALREADY_EXISTS** A trap with the same *kind* and *addr* already exists.

**ZX_ERR_BAD_HANDLE** *guest* or *port* are invalid handles.

**ZX_ERR_INVALID_ARGS** *kind* is not a valid address space, *addr* or *len*
do not meet the requirements of *kind*, *len* is 0, or *ZX_GUEST_TRAP_MEM* was
specified with a *port*.

**ZX_ERR_NO_MEMORY** Temporary failure due to lack of memory.

**ZX_ERR_OUT_OF_RANGE** The region specified by *addr* and *len* is outside of
of the valid bounds of the address space *kind*.

**ZX_ERR_WRONG_TYPE** *guest* is not a handle to a guest, or *port* is not a
handle to a port.

## NOTES

*ZX_GUEST_TRAP_BELL* shares the same address space as *ZX_GUEST_TRAP_MEM*.

On x86-64, if *kind* is *ZX_GUEST_TRAP_BELL* or *ZX_GUEST_TRAP_MEM* and *addr*
is the address of the local APIC, then *len* must be equivalent to the size of a
page. This is due to a special page being mapped when a trap is requested at the
address of the local APIC. This allows us to take advantage of hardware
acceleration when available.

## SEE ALSO

[guest_create](guest_create.md),
[port_create](port_create.md),
[port_wait](port_wait.md),
[vcpu_create](vcpu_create.md),
[vcpu_resume](vcpu_resume.md),
[vcpu_interrupt](vcpu_interrupt.md),
[vcpu_read_state](vcpu_read_state.md),
[vcpu_write_state](vcpu_write_state.md).
