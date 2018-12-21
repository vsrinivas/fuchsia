# Pager

## NAME

pager - Mechanism for userspace paging

## SYNOPSIS

Pagers provide a mechanism for a userspace process to provide demand paging for VMOs.

## DESCRIPTION

A pager object allows a userspace pager service (typically a filesystem) to create VMOs that serve
as in-memory caches for external data. The kernel generates page requests based on accesses to the
VMOs, which the pager service is then responsible for fulfilling. The kernel does not do
prefetching; it is the responsibility of the pager service to implement any applicable prefetching.

It is possible for a single pager to simultaniously back multiple vmos. It is also possible for
multiple independent pager objects to exist simultaniously.

Creating a pager is not a privileged operation. However, the default behavior of syscalls which
operate on VMOs is to fail if the operation would require blocking on IPC back to a userspace
process, so applications generally need to be aware of when they are operating on pager owned
VMOs. This means that that services which provide pager owned VMOs to clients should be explicit
about doing so as part of their API.

TODO(stevensd): Update description once writeback is implemented.

## SEE ALSO

+ [vm_object](vm_object.md) - Virtual Memory Objects

## SYSCALLS

+ [pager_create](../syscalls/pager_create.md) - create a new pager object
+ [pager_create_vmo](../syscalls/pager_create_vmo.md) - create a vmo owned by a pager
+ [pager_detach_vmo](../syscalls/pager_detach_vmo.md) - detaches a pager from a vmo
+ [pager_supply_pages](../syscalls/pager_supply_pages.md) - supply pages into a pager owned vmo
