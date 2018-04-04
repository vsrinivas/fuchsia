# Bus Transaction Initiator

## NAME

bus_transaction_initiator - DMA configuration capability

## SYNOPSIS

Bus Transaction Initiators (BTIs) represent the bus mastering/DMA capability
of a device, and can be used for granting a device access to memory.

## DESCRIPTION

Device drivers are provided one BTI for each bus transaction ID each of its
devices can use.  A bus transaction ID in this context is a hardware transaction
identifier that may be used by an IOMMU (e.g. PCI addresses on Intel's IOMMU
and StreamIDs on ARM's SMMU).

A BTI can be used to pin memory used in a Virtual Memory Object (VMO).
If a caller pins memory from a VMO, they are given device-physical addresses
that can be used to issue memory transactions to the VMO (provided the
transaction has the correct bus transaction ID).  If transactions affecting
these addresses are issued with a different transaction ID, the transaction
may fail and the issuing device may need a reset in order to continue functioning.

A BTI manages a list of quarantined PMTs.  If a PMT was created from a BTI using
**bti_pin**(), and the PMT's handle is released without **pmt_unpin**() being
called, the PMT will be quarantined.  Quarantined PMTs will prevent their
underlying physical memory from being released to the system for reuse, in order
to prevent DMA to memory that has since been reallocated.  The quarantine may be
cleared by invoking **bti_release_quarantine**().

TODO(teisenbe): Add details about failed transaction notification.

## SEE ALSO

+ [pmt](pinned_memory_token.md) - Pinned Memory Tokens
+ [vm_object](vm_object.md) - Virtual Memory Objects

## SYSCALLS

+ [bti_create](../syscalls/bti_create.md) - create a new bus transaction initiator
+ [bti_pin](../syscalls/bti_pin.md) - pin memory and grant access to it to the BTI
+ [bti_release_quarantine](../syscalls/bti_release_quarantine.md) - release quarantined PMTs
+ [pmt_unpin](../syscalls/pmt_unpin.md) - revoke access and unpin memory
