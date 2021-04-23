# How to implement a user pager

## Introduction

Fuchsia filesystems are implemented in userspace so Fuchsia exposes a way to implement pager-backed
memory in userspace. As client programs access memory, the kernel will send requests to the
associated pager to populate the corresponding memory.

Non-filesystem programs can also implement a pager. With the addition of another thread, any
userspace program can implement paging functionality for a memory region. This can enable
significant flexibility for dynamically-populated data regions.

Most filesystems will use the [vfs](/src/lib/storage/vfs) library which implements these concepts
and exposes them to the filesystem in an easier-to-use interface. Using the "vfs" library is
recommended when possible to implement filesystems.

This document describes the low-level implementation details of a writing a pager from scratch.

## Requesting a pager-backed VMO

Paged [Virtual Memory Objects](/docs/concepts/kernel/concepts.md) (VMOs) can be created and
transferred in any way the implementation desires. Most mappings are created by filesystems which
map memory in response to `GetBuffer` requests on the
[fuchsia.io/File](/docs/reference/fidl/fuchsia.io) interface. (Most client programs will make this
IPC request indirectly through the Posix `mmap` interface.)

## Creating a pager-backed VMO

For general setup:

  * Create a [pager](/docs/reference/kernel_objects/pager) kernel object with
    [zx\_pager\_create](/docs/reference/syscalls/pager_create) (or `zx::pager::create` in C++). This
    object will be used to create individual VMOs. An example of this setup is in the
    [paged_vfs.cc](/src/lib/storage/vfs/cpp/pager-backed.cc) implementation.

  * Create a thread or pool of threads to respond to paging requests. You can use the
    [async loop](/zircon/system/ulib/async/include/lib/async/cpp/paged_vmo.h), but if all your
    thread does is respond to paging requests, it can be simpler to create a port and wait on it
    manually. An example is in
    [pager\_thread\_pool.cc](/src/lib/storage/vfs/cpp/pager_thread_pool.cc) implementation.

To create a pager-backed vmo, call
[zx\_pager\_create\_vmo](/docs/reference/syscalls/pager_create_vmo) and supply the size, the port
that you will use to wait on page requests, and a unique ID for your code to associate requests with
(these are not used by the kernel; they will come out as the `key` in the `zx_port_packet_t` that
you read from the port).

This pager-backed vmo should never be directly written or read to by the code backing the pager.
Doing so will cause the kernel to try to page in the data which will re-enter the pager. This
vmo will instead be populated on demand using a special API described below.

At this time, the pager implementation should also register a watcher for "no clones" of the
pager-backed VMO (see "Freeing the paged VMO" below).

### Vending a pager-backed VMO

A pager would create one pager-backed VMO for each file (or equivalent concept), but typically
there can be multiple consumers of the same file. To implement this, the pager would keep a single
pager-backed VMO for each file and send clones of this VMO to each consumer. Using clones is also
important to know when the clients are done with the mappings (see "Freeing the pager-backed VMO"
below).

```c++
  zx::vmo clone;
  paged_vmo.create_child(ZX_VMO_CHILD_SNAPSHOT_AT_LEAST_ON_WRITE, 0, size, &clone);
```

If the paging data is executable code, it must be marked with the executable permission. Such
permission is not generally available to user programs. Filesystems that need to vend executable
pages will be passed a "vmex" [resource](/docs/reference/kernel_objects/resource). The clone is
marked executable by calling
[zx\_replace\_as\_executable](/docs/reference/syscalls/vmo_replace_as_executable) to replace the
clone's handle with one that has the executable permission.

## Responding to paging requests

Page requests for a pager-backed VMO are delivered on the port associated with the
`zx_pager_create_vmo()` call that created it. They will come in with a packet type of
`ZX_PKT_TYPE_PAGE_REQUEST` and the `key` will be the unique ID supplied at creation time. The
pager would use the ID to lookup the information required for the object. An example is in the
[pager\_thread\_pool.cc](/src/lib/storage/vfs/cpp/pager_thread_pool.cc) implementation.

The pager responds to the request by either populating the requested range of the VMO or by marking
it as failed. The pager must always report the entire requested range as either populated or failed
or the thread that triggered the page request will hang forever waiting on the page fault to
complete.

### Reporting errors

Pager errors are reported by calling [zx\_pager\_op\_range](/docs/reference/syscalls/pager_op_range)
with `ZX_PAGER_OP_FAIL`. It is passed the pager-backed VMO handle, the offset and length originally
requested by the kernel, and an error value. Importantly, the error value must be one of several
known values (see the syscall documentation).

### Supplying pager-backed data

To supply data, the pager creates an "aux VMO" that holds the data being transmitted to the kernel
for writing into the pager-backed vmo. This is a normal non-pager-backed VMO that is only used for the
unique communication of data from a pager to the kernel. The pager then calls
[zx\_pager\_supply\_pages](/docs/reference/syscalls/pager_supply_pages) with the aux vmo and the
data range requested by the kernel in the page request.

Importantly, [zx\_pager\_supply\_pages](/docs/reference/syscalls/pager_supply_pages) enforces some
requirements of the aux vmo, including that it must not be mapped during the call. You can populate
it by mapping, writing to it, and then unmapping, or using `zx_vmo_write`. You can re-use the same
aux vmo for every page request, or create a new one each time. The unique requirements of the aux
vmo allow the pages of the aux vmo to be spliced into the pager-backed VMO without copying.

Attempting to write directly into the pager-backed VMO, or even indirectly with `zx_vmo_write()` will cause
pager requests and will reenter the pager. The use of the aux vmo and the special
`zx_pager_supply_pages()` function avoid this problem.

## Freeing the pager-backed VMO

When there are no mapped clones of the pager-backed vmo, it can be deleted. A pager implementation
watches for the "no clones" notification of the main pager-backed VMO to know when this happens. For
an example, see `PagedVnode::WatchForZeroVmoClones()` in [paged\_vnode.cc](/src/lib/storage/vfs/cpp/paged_vnode.cc).

One thing to keep in mind is that the kernel will queue this message for delivery. In the meantime,
it might be possible for the pager to create a new clone of the VMO. As a result, the message
handler should verify there are actually no clones before doing any cleanup. An example can be seen
in the `PagedVnode::OnNoPagedVmoClonesMessage()` in
[paged\_vnode.cc](/src/lib/storage/vfs/cpp/paged_vnode.cc).

### Freeing a pager-backed VMO when there are still clones

In some implementations, it might be possible for the file (or other backing data) to be destroyed
before the clients of the pager-backed data. This can result in page requests getting delivered to
the port for objects that are destroyed. Such implementations should be sure to handle this case
(possibly by validating the unique pager ID and silently returning if there is no corresponding
pager).

Although not always necessary, it is also possible to cleanly detach such that the kernel guarantees
that no more pager requests will be forthcoming. To avoid races with already-queued page requests,
this operation is a two-step process.

  * The pager calls [zx\_pager\_detach\_vmo](/docs/reference/syscalls/pager_detach_vmo).
  * The kernel will enqueue a port packet with the type `ZX_PAGER_VMO_COMPLETE` for that object.
    After this message is received, no more messages will be received for that VMO.
