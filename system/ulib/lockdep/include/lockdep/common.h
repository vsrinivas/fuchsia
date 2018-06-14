// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//
// Common definitions for the lockdep library.
//

#pragma once

#include <stddef.h>
#include <stdint.h>

#include <fbl/type_support.h>

#include <lockdep/runtime_api.h>

namespace lockdep {

// Configures the maximum number of dependencies each lock class may have. A
// system may override the default by globally defining this name to the desired
// value. This value is automatically converted to the next suitable prime.
#ifndef LOCK_DEP_MAX_DEPENDENCIES
#define LOCK_DEP_MAX_DEPENDENCIES 31
#endif

// Configures whether lock validation is enabled or not. Defaults to disabled.
// When disabled the locking utilities simply lock the underlying lock types,
// without performing any validation operations.
#ifndef LOCK_DEP_ENABLE_VALIDATION
#define LOCK_DEP_ENABLE_VALIDATION 0
#endif

// Id type used to identify each lock class.
using LockClassId = uintptr_t;

// A sentinel value indicating an empty slot in lock tracking data structures.
constexpr LockClassId kInvalidLockClassId = 0;

namespace internal {

// Returns a prime number that reasonably accomodates a hash table of the given
// number of entries. Each number is selected to be slightly less than twice the
// previous and as far as possible from the nearest two powers of two.
constexpr size_t NextPrime(size_t n) {
    if (n < (1 << 4))
        return 23;
    else if (n < (1 << 5))
        return 53;
    else if (n < (1 << 6))
        return 97;
    else if (n < (1 << 7))
        return 193;
    else if (n < (1 << 8))
        return 389;
    else if (n < (1 << 9))
        return 769;
    else if (n < (1 << 10))
        return 1543;
    else
        return 0; // The input exceeds the size of this prime table.
}

} // namespace internal

// The maximum number of dependencies each lock class may have. This is the
// maximum branching factor of the directed graph of locks managed by this
// lock dependency alogrithm. The value is a prime number selected to optimize
// the hash map in LockDependencySet.
constexpr size_t kMaxLockDependencies = internal::NextPrime(LOCK_DEP_MAX_DEPENDENCIES);

// Check to make sure that the requested max does not exceed the prime table.
static_assert(kMaxLockDependencies != 0, "LOCK_DEP_MAX_DEPENDENCIES too large!");

// Whether or not lock validation is globally enabled.
constexpr bool kLockValidationEnabled = static_cast<bool>(LOCK_DEP_ENABLE_VALIDATION);

// Utility template alias to simplify selecting different types based whether
// lock validation is enabled or disabled.
template <typename EnabledType, typename DisabledType>
using IfLockValidationEnabled = typename fbl::conditional<kLockValidationEnabled,
                                                          EnabledType,
                                                          DisabledType>::type;

// Result type that represents whether a lock attempt was successful, or if not
// which check failed.
enum class LockResult : uint8_t {
    Success,
    AlreadyAcquired,
    OutOfOrder,
    InvalidNesting,
    InvalidIrqSafety,

    // Non-fatal error that indicates the dependency hash set for a particular
    // lock class is full. Consider increasing the size of the lock dependency
    // sets if this error is reported.
    MaxLockDependencies,

    // Internal error value used to differentiate between dependency set updates
    // that add a new edge and those that do not. Only new edges trigger loop
    // detection.
    DependencyExists,
};

// Returns a string representation of the given LockResult.
static inline const char* ToString(LockResult result) {
    switch (result) {
    case LockResult::Success:
        return "Success";
    case LockResult::AlreadyAcquired:
        return "Already Acquired";
    case LockResult::OutOfOrder:
        return "Out Of Order";
    case LockResult::InvalidNesting:
        return "Invalid Nesting";
    case LockResult::InvalidIrqSafety:
        return "Invalid Irq Safety";
    case LockResult::MaxLockDependencies:
        return "Max Lock Dependencies";
    case LockResult::DependencyExists:
        return "Dependency Exists";
    default:
        return "Unknown";
    }
}

} // namespace lockdep
