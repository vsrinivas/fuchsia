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
+ `concurrent::WellDefinedCopyable<T>::Update(const T&, SyncOptType<SyncOpt>)`
+ `concurrent::WellDefinedCopyable<T>::Read(T&, SyncOptType<SyncOpt>)`

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

### `WellDefinedCopyable<T>`

In order to make life a bit easier for users who need copy data in a well
defined way into and out of structures, a helper class named
`WellDefinedCopyable<T>` is offered.

Users may wrap any trivially copyable type, `T` in one of these wrapper
instances, and the use the provided Update and Read methods to copy data
into and out of the contained `T` instance, respectively.  These methods, by
design, deliberately restrict the ways that the user can gain access to the
underlying storage, forcing them make use of the lowest level well-defined
transfer functions.

Constructor parameters are directly forwarded to the underlying `T` instance.

```c++
WellDefinedCopyable<Foo> default_constructed;
WellDefinedCopyable<Foo> explicit_construction{45};
WellDefinedCopyable<Foo> moar_args{"Foo", 45, "Bar", 34.4};
```

Just remember that `T` (and therefore all of its data members) must be trivially
copyable.

#### Explicit synchronization

The wrapper's `Update` and `Read` methods allow the user to specify the
synchronization option to use, with a default of `SyncOpt::AcqRelOps`, just like
the `WellDefinedCopy(To|From)` functions do.  Because of the somewhat awkward
dependent name rules of C++, the type of explicit synchronization desired can be
specified using a type-tagging pattern, instead of needing to specify the sync
option as an explicit template parameter.

```c++
WellDefinedCopyable<Foo> shared_foo;
Foo my_foo;

shared_foo.Read<SyncOpt::Fence>(my_foo);          // this does not work.
shared_foo.template Read<SyncOpt::Fence>(my_foo); // this is one way to make it work.
shared_foo.Read(my_foo, SyncOpt_Fence);           // this reads a bit better.
```

Aliases for the synchronization type tags are as follows.
| enum class         | type tag instance |
|--------------------|-------------------|
| SyncOpt::AcqRelOps | SyncOpt_AcqRelOps |
| SyncOpt::Fence     | SyncOpt_Fence     |
| SyncOpt::None      | SyncOpt_None      |


#### Raw storage access

User don't always _have_ to access their `T` instance's storage only by copying
data into or out of it.  Direct read-only access may be obtained using the
`WellDefinedCopyable<T>`'s `unsynchronized_get` method, however users should
exercise caution when choosing to do this.

Accessing the buffer using `unsynchronized_get` is _only_ safe if the user can
guarantee that no write operations may be concurrently performed against the
storage while the user is reading the instance.

One example of a legitimate use of this method might be when a user is operating
in the write exclusive portion of a sequence lock.  They are guaranteed to be
the only potential writer of the wrapped object, so while it is still important
that they continue to use `Update` when they wish to mutate their instance of
`T`, it is OK for them to read `T` directly without using `Read` as this
will not cause any undefined behavior when done concurrently with other readers
in the system.

## `SeqLock`

### Overview

Sequence locks are synchronization primitives which allow for concurrent read
access to a set of data without ever excluding write updates.  Reads are
performed as transactions, which succeed if and only if there is no concurrent
write operation which overlaps with the read transaction.  Sequence locks can be
useful in patterns where any of the following conditions hold:

+ Read operations are expected to greatly out-number write operations, and high
  levels of read concurrency are desired.
+ Write operations must never be delayed by concurrent read operations.
+ Readers have strictly read-only access to the shared state of the published
  data.  No modifications of the state are allowed, as would be required when
  using an synchronization primitive such as a mutex.

To assist in implementing data sharing via a sequence lock, `libconcurrent`
offers the `SeqLock` primitive.  The `SeqLock` behaves like a spinlock when
acquired exclusively for write access, while still allowing concurrent read
transactions to take place.

### Rules

In order to properly implement a sequence lock pattern, there are a few rules
which must be obeyed at all times.

1) Data protected by a `SeqLock` may be both read and written concurrently,
therefore care must be taken to always access the data in a way which is free
from data races.
2) Reads of, and writes to the data protected by the `SeqLock` must always
properly synchronize-with the internals of the lock in order to ensure proper
behavior on architectures with weak memory ordering.
3) No decisions based on protected data should ever be made _during_ a read
transaction.  It is only after a read transaction has _successfully_ concluded
that a program is guaranteed to have made a coherent observation of the
protected data.

Rules #1 and #2 can be easily satisfied by always accessing protected data using
the `WellDefinedCopy` primitives described above.

### Example

Here is a small example of how it looks to use a SeqLock.

```c++
class MyClass {
 public:
  void Update(const Foo& foo) {
    seq_lock_.Acquire();  // Acquire the lock for exclusive write access.
    foo_.Update(foo);     // The wrapper ensures data race free access, as well
                          // as properly synchronizing-with the lock.
    seq_lock_.Release();  // Release the lock to allow concurrent read
                          // trasnactions to succeed.
  }

  Foo Observe() {
    Foo ret;
    concurrent::SeqLock::ReadTransactionToken token;
    do {
      // Start a new read transaction.  Note that this operation will spin if
      // there happens to be write transaction taking place at the same time. Be
      // sure to use one of the TryBeginReadTransaction forms along with a
      // timeout if this is a time sensitive code path where the observation
      // operation may need to be aborted if it is taking too long.
      token = seq_lock_.BeginReadTransaction();

      // Make a local copy of the data using the WellDefinedCopyWrapper to
      // ensure that we have no data races, and that we properly
      // synchronize-with the lock and any concurrent write operations.
      foo_.Read(ret);

      // End the transaction, and check to see if it succeeded.  If it didn't
      // then an update operation must have overlapped with this observation
      // operation.  Keep trying until we manage to make a coherent observation
      // with no overlapping concurrent write.
    } while (!seq_lock_.EndReadTransaction(token));

    // Our read transaction was successful.  It is now OK to make decisions
    // based on our results, stored in |ret|
    return ret;
  }

 private:
  concurrent::SeqLock seq_lock_;
  concurrent::WellDefinedCopyWrapper<Foo> foo_ TA_GUARDED(seq_lock_);
};
```

### Blocking behavior

Both the `BeginReadTransaction` and the `Acquire` operations have the potential
to spin-wait if there happens to be another thread which has currently
`Acquire`ed the lock for exclusive access. Technically, the operations never
result in the thread blocking in the scheduler, however they will spin waiting
for the lock to become uncontested before proceeding.

If users are executing in a time sensitive context, or a read operation is being
conducted against data which is being updated by another (potentially malicious)
process, `Try` versions of the `BeginReadTransaction` and the `Acquire` may be
used along with a timeout to limit the amount of spinning which may eventually
take place.

+ `bool TryBeginReadTransaction(ReadTransactionToken& out_token, zx_duration_t timeout)`
+ `bool TryBeginReadTransactionDeadline(ReadTransactionToken& out_token, zx_time_t deadline)`
+ `bool TryAcquire(zx_duration_t timeout)`
+ `bool TryAcquireDeadline(zx_time_t deadline)`
