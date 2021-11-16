# libconcurrent

## Overview

libconcurrent is a small library intended to be used both by kernel mode and
user mode code to assist in implementing memory sharing patterns which involve
readers observing memory concurrently with writers updating the same memory.
Unlike techniques which use exclusion (such as mutexes, reader-writer locks,
spinlocks, and so on) to ensure that reads to memory locations are never taking
place at the same time as writes, concurrent read-write patterns require some
extra care to ensure that programs are formally "data-race free" according to
the C++ specification.

## Data races in C++

Section 6.9.2.1 of the [C++20 Draft Specification](http://www.open-std.org/JTC1/SC22/WG21/docs/papers/2020/n4849.pdf)
formally defines what is considered to be a "data race" by the standard.
Paragraph 2 defines a "conflict" by saying:

```
Two expression evaluations conflict if one of them modifies a memory location
(6.7.1) and the other one reads or modifies the same memory location.
```

Paragraph 20 then defines a "data race" by saying:

```
Two actions are potentially concurrent if

(21.1) — they are performed by different threads, or
(21.2) — they are unsequenced, at least one is performed by a signal handler,
         and they are not both performed by the same signal handler invocation.

The execution of a program contains a data race if it contains two potentially
concurrent conflicting actions, at least one of which is not atomic, and neither
happens before the other, except for the special case for signal handlers
described below. Any such data race results in undefined behavior.
```

So, when sharing data between concurrently executing threads where mutual exclusion
cannot be used to ensure that data races cannot occur, extra care must be taken
when reading and modifying the shared data.  The data transfer utilities offered
by libconcurrent ensure that all load and stores executed on shared memory
regions are done using atomics, which should be sufficient to avoid introducing
any formal data races (and therefore undefined behavior) to a program.

## Transferring data

The lowest level building blocks offered by libconcurrent give the user the
ability to concurrently transfer data into and out of a share memory location
without accidentally introducing any undefined behavior as a result of
unintentional data races.  These building blocks are:

+ `concurrent::WellDefinedCopyTo<SyncOpt, Alignment>(void* dst, const void* src, size_t len)`
+ `concurrent::WellDefinedCopyFrom<SyncOpt, Alignment>(void* dst, const void* src, size_t len)`

`CopyTo` operations move data from a thread's private buffer into a shared
buffer which may be accessed concurrently by readers. `CopyFrom` operations move
data from a shared buffer which may be concurrently written to into a thread's
private buffer.

Copy(To|From) both have memcpy semantics, not memmove semantics. In other
words, it is illegal for |src| or |dst| to overlap in any way.

The individual `Copy(To|From)` functions represent the absolute
lowest level building blocks of libconcurrent, and need to be used with care.
When possible, prefer using one of the higher level building blocks which help
to automate common patterns.  They have `memcpy` compatible signatures, but
include a few additional requirements.  Specifically:

1) The `src` and `dst` buffers *must* have the same alignment relative to the
   maximum atomic operation transfer granularity (currently 8 bytes).  In other
   words, `ASSERT((src & 0x7) == (dst & 0x7))`.
2) If these functions are being used to copy instances of structures or classes,
   it is required that those structs/classes be trivially copyable.
3) When accessing shared memory, it is important that all read and write
   accesses operate using the same alignment and op-width at all times.  For
   example:  Given a shared buffer located at an 8 byte aligned address,
   `shared`, the following operations may all be safely conducted concurrently:
   + `CopyTo(shared, local_1, 16);`
   + `CopyFrom(local_2, shared, 16);`
   + `CopyFrom(local_3, shared + 8, 8);`
   Since `shared` is 8 byte aligned, all of the accesses to shared will also all
   be 8 byte aligned, and of 8 bytes in length, guaranteeing that all memory
   accesses to the same address in the `shared` region will use the same width
   memory transaction.  Adding a `CopyFrom(local_4, shared + 4, 8);` to the list
   of concurrent operations, however, would _not_ be safe as it would result in
   two 4 byte read transactions being conducted against the shared buffer,
   overlapping regions where 8 byte transactions are also taking place.

### Synchronization Options

By default, `Copy(To|From)` operations to the same regions of memory attempt to
synchronize-with each other by adding `memory_order_release` semantics to each
of the atomic store operations executed during a call to `CopyTo`, and
`memory_order_acquire` semantics to each of the atomic load operations executed
during a call to `CopyFrom`.

Depending on the use case, however, it is possible that synchronizing like this
might not be as efficient as using a thread fence instead.  Users may control
the synchronization behavior of the operation using the first template parameter
of the call to `Copy(To|From)`, which must be a member of the `SyncOpt`
enumeration.  The options are as follows:

1) `SyncOpt::AcqRelOps`.  This is the default option, and causes each atomic
   load/store, to use `memory_order_acquire`/`memory_order_release`, as
   appropriate, during the transfer.
2) `SyncOpt::Fence`.  Instead of synchronizing each of the load/store operations
   individually, all atomic load/stores are executed with
   `memory_order_relaxed`, and thread fences are used instead.  `CopyTo`
   operations will be proceeded by a `memory_order_release` thread fence, while
   `CopyFrom` operations will be followed with a `memory_order_acquire` thread
   fence.
3) `SyncOpt::None`.  No synchronization will be added to the transfer operation.
   No explicit thread fences will be generated, and all atomic load/store
   operations will use `memory_order_relaxed`.

Extreme care should be taken when using `SyncOpt::None`.  It is almost always
the case that at least _some_ synchronization will be needed when publishing and
consuming data concurrently.  The `SyncOpt::None` option is offered for users
who may need to move data to/from multiple disjoint regions of shared memory,
and wish to use fences to achieve synchronization.  In this case, the first/last
(CopyTo/CopyFrom) operation in the sequence should use a fence, while other
operations would choose `SyncOpt::None` avoiding the need for superfluous
load/store or thread fence synchronization.  For example:

```c++
void PublishData(const Foo& foo1, const Foo& foo2, const Bar& bar) {
  WellDefinedCopyTo<SyncOpt::Fence>(&shared_foo1, &foo1, sizeof(foo1));
  WellDefinedCopyTo<SyncOpt::None>(&shared_foo2, &foo2, sizeof(foo2));
  WellDefinedCopyTo<SyncOpt::None>(&shared_bar1, &bar1, sizeof(bar1));
}

void ObserveData(Foo& foo1, Foo& foo2, Bar& bar) {
  WellDefinedCopyFrom<SyncOpt::None>(&foo1, &shared_foo1, sizeof(foo1));
  WellDefinedCopyFrom<SyncOpt::None>(&foo2, &shared_foo2, sizeof(foo2));
  WellDefinedCopyFrom<SyncOpt::Fence>(&bar1, &shared_bar1, sizeof(bar1));
}
```

### Alignment Optimizations

If alignment of a transfer can be compile-time guaranteed to be greater than or
equal to the maximum atomic transfer granularity of 8 bytes, a minor
optimization can be achieved during the transfer by skipping the transfer phase
which brings the operation into 8 byte alignment.  Users can access this
optimization by specifying the guaranteed alignment of their operation as the
second template parameter of the `Copy(To|From)` operation.  For example:

```c++
template <typename T>
void PublishData(const T& src, T& dst) {
  WellDefinedCopyTo<SyncOpt::AcqRelOps, alignof(T)>(&dst, &src, sizeof(T));
}

template <typename T>
void ObserveData(const T& src, T& dst) {
  WellDefinedCopyFrom<SyncOpt::AcqRelOps, alignof(T)>(&dst, &src, sizeof(T));
}
```
