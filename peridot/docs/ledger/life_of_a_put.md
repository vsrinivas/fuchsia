# Life of a Put

This document explains how Ledger works by following the mechanics of a single
Put operation.

The Putting of a value happens in 2 major stages:
1. Locally updating the btree to take into account the new key-value.
2. Synchronizing the new key-value with the cloud and other devices.

## 1. Local update
The client facing API of Ledger is defined in [ledger.fidl].
There are multiple methods to do put operations, with slightly different
semantics. For this example we can use the FIDL method
``Put(array<uint8> key, array<uint8> value) => (Status status);``.

This FIDL interface is implemented by PageImpl, which takes a generated callback
type (PutCallback) used to pass the completion status back to the client.

### Storing the Value to disk
The first step to handling the FIDL call is storing the *value* (which can be
large, e.g. multiple GB) onto the disk. The value (which can be passed as a byte
array, a [VMO], or a socket) is wrapped in a DataSource.
The data in the DataSource is cut in chunks (see [dynamic chunking] for an
explanation of the idea, and the [splitting implementation] for details).
The chunks not already on disks are then written to disk.

*** note
**A note regarding writing to disk:** Ledger writes all its data to disk using
[LevelDB]. LevelDB's cache makes reading and writing data faster than when using
the filesystem.
***

### Draining the pending PageWatcher updates
Clients can register PageWatchers to observe changes to Pages. Before creating
the [commit], we drain the pending updates to the PageWatchers registered for
the given Page connection. After draining the pending updates, we stop the
BranchTracker from sending new updates until we've handled the change.

If the Put is part of a transaction, we wait for all PageWatchers to have
handled the updates before continuing to the next step. The PageWatchers say
that they've handled the OnChange by calling the (generated) OnChangeCallback.

### Journals
All changes are first written in a Journal before being committed. There are two
types of journals: *explicit* journals when clients starts a transaction, and
*implicit* journals when clients do a change without having started a
transaction first.

Changes in explicit journals are guaranteed to be seen together: a PageSnapshot
and/or a PageWatcher will either contain/see **all** the changes, or none. If
for some reason Ledger is unexpectedly closed, any non completed explicit
transaction is dropped.

In contrast, PageSnapshots and/or PageWatchers may see the individual changes
from an implicit journal, and if Ledger is unexpectedly closed any in-progress
implicit journal will be committed during the next startup.

### Finishing a transaction
When a transaction is completed, a commit is appended to one of the Page's
heads.

The BranchTracker of the Page connection is informed of this new commit as to
make sure it tracks that branch and not some other branch. Not doing so would
make changes seem to “disappear” as clients would see a different branch than
the one with their change. The BranchTracker then notifies the relevant
registered Watchers of the change.
If there are multiple heads, the MergeResolver starts the process of merging the
heads.

## 2. Synchronization
TODO
### Cloud sync
### P2P sync

[ledger.fidl]: /public/fidl/fuchsia.ledger/ledger.fidl
[VMO]: https://fuchsia.googlesource.com/zircon/+/master/docs/objects/vm_object.md
[dynamic chunking]: https://github.com/YADL/yadl/wiki/Rabin-Karp-for-Variable-Chunking
[splitting implementation]: /bin/ledger/storage/impl/split.cc
[LevelDB]: https://en.wikipedia.org/wiki/LevelDB
[commit]: architecture.md#storage
