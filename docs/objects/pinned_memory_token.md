# Pinned Memory Token

## NAME

pinned_memory_token - Representation of a device DMA grant

## SYNOPSIS

Pinned Memory Tokens (PMTs) represent an outstanding acccess grant to a device
for performing DMA.

## DESCRIPTION

PMTs are obtained by [pinning memory with a BTI object](../syscalls/bti_pin.md).
It is valid for the device associated with the BTI to access the memory represented
by the PMT for as long as the PMT object is around.  When the PMT object is
destroyed, either via **zx_handle_close**(), **zx_pmt_unpin**(), or process
termination, access to the represented memory becomes illegal (this is
enforced by hardware on systems with the capability to do so, such as IOMMUs).

TODO(teisenbe): Describe quarantining

## SEE ALSO

+ [bus_transaction_initiator](bus_transaction_initiator.md) - Bus Transaction Initiators

## SYSCALLS

+ [bti_pin](../syscalls/bti_pin.md) - pin memory and grant access to it to the BTI
+ [pmt_unpin](../syscalls/pmt_unpin.md) - revoke access and unpin memory
