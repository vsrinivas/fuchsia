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

A BTI can be used to pin and unpin memory used in a Virtual Memory Object (VMO).
If a caller pins memory from a VMO, they are given device-physical addresses
that can be used to issue memory transactions to the VMO (provided the
transaction has the correct bus transaction ID).  If transactions affecting
these addresses are issued with a different transaction ID, the transaction
may fail and the issuing device may need a reset in order to continue functioning.

TODO(teisenbe): Add details about failed transaction notification.

## SEE ALSO

+ [vm_object](vm_object.md) - Virtual Memory Objects

## SYSCALLS

+ [bti_create](../syscalls/bti_create.md) - create a new bus transaction initiator
+ [bti_pin](../syscalls/bti_pin.md) - pin memory and grant access to it to the BTI
+ [bti_unpin](../syscalls/bti_unpin.md) - revoke access and unpin memory
