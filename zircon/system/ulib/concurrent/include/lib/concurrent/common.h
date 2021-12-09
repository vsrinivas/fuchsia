// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_CONCURRENT_COMMON_H_
#define LIB_CONCURRENT_COMMON_H_

#include <stdint.h>
#include <zircon/assert.h>

#include <memory>

namespace concurrent {

// An enumeration of various synchronization options to use when performing
// memory transfer operations with WellDefinedCopy(To|From).
//
// :: AcqRelOps ::
// Use either memory_order_acquire (CopyFrom) or memory_order_release (CopyTo)
// on every atomic load/store operation during the transfer to/from the shared
// buffer.
//
// :: Fence ::
// Use either a memory_order_acquire thread fence (CopyFrom) after the transfer
// operation, or a memory_order_release (CopyTo) thread fence before the
// operation, and memory_order_relaxed for each of the atomic load/store
// operations during the transfer.
//
// :: None ::
// Simply use memory_order_relaxed for each of the atomic load/store
// operations during the transfer.  Do not actually introduce any
// explicit synchronization behavior.
//
// WARNING: Use cases for this transfer mode tend to unusual.  Users will almost
// always want some form of synchronization to take place during their
// transfers.  One example of where it may be appropriate to use SyncOpt::None
// might be a situation where users are attempting to observe the state of more
// than one object while inside of a sequence lock read transaction, and the
// user has decided that it is better to use a thread fence than to use acquire
// semantics on each element transferred.  Such a sequence might look something
// like this:
//
// Foo foo1, foo2;
// Bar bar1, bar2;
// ...
// WellDefinedCopyFrom<SyncOpt::None, alignof(Foo)>(&foo1, &src_foo1, sizeof(foo1));
// WellDefinedCopyFrom<SyncOpt::None, alignof(Foo)>(&foo2, &src_foo2, sizeof(foo2));
// WellDefinedCopyFrom<SyncOpt::None, alignof(Foo)>(&bar1, &src_bar1, sizeof(bar1));
// WellDefinedCopyFrom<SyncOpt::Fence, alignof(Foo)>(&bar2, &src_bar2, sizeof(bar2));
//
// Note that it is the _last_ transfer operation which includes the fence.  In
// the case of a CopyTo operation (when publishing data) it would be the _first_
// operation which included the fence, not the last.
enum class SyncOpt { AcqRelOps, Fence, None };

// Define tag types and constexpr instances of them which allow us to parameter
// based template deduction.  Mostly, this is about working around some of C++'s
// otherwise painful syntax.  It allows us to say:
//
// WellDefinedCopyable<Foo> wrapped_foo;
// Foo foo;
// wrapped_foo.CopyFrom(foo, SyncOpt_Fence);            // This
// wrapped_foo.template CopyFrom<SyncOpt::Fence>(foo);  // Instead of this
//
// Without this, having to put the `template` keyword after the `.` but before
// the `<>`'s is just a sad and painful fact of C++ template lyfe.
template <SyncOpt>
struct SyncOptType {};
constexpr SyncOptType<SyncOpt::AcqRelOps> SyncOpt_AcqRelOps;
constexpr SyncOptType<SyncOpt::Fence> SyncOpt_Fence;
constexpr SyncOptType<SyncOpt::None> SyncOpt_None;

namespace internal {

constexpr size_t kMaxTransferGranularity = sizeof(uint64_t);
enum class CopyDir { To, From };
enum class MaxTransferAligned { No, Yes };

}  // namespace internal
}  // namespace concurrent

#endif  // LIB_CONCURRENT_COMMON_H_
