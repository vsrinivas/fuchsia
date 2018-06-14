// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <zircon/compiler.h>

#include <fbl/type_support.h>

#include <lockdep/global_reference.h>

namespace lockdep {

// Tags the given lock type with an option name and a policy type. The policy
// type describes how to acquire and release the lock type and whether or not
// extra state must be stored to correctly operate the lock (i.e. IRQ state in
// spinlocks). The option name permits selecting different lock policies to
// apply when acquiring and releasing the lock (i.e. whether or not to save IRQ
// state when taking a spinlock).
//
// Arguments:
//  lock_type  : The type of the lock to specify the policy for. This type is
//               passed to the LockType argument of Guard<LockType, Option>
//               when acquiring this lock type.
//  option_name: A type tag to associate with this lock type and policy. This
//               type is passed to the Option argument of
//               Guard<LockType, Option> to select this policy instead of
//               another policy for the same lock type.
//  lock_policy: The policy to use when Guard<LockType, Option> specifies the
//               lock_type and option_name arguments given with this policy.
//
// This macro essentially creates a map from the tuple (lock_type, option_name)
// to lock_policy when instantiating Guard<lock_type, option_name>.
//
// Every lock policy must specify a public nested type named |State| that stores
// any state required by the lock acquisition. If state is not required then the
// type may be an empty struct. Every lock policy must also define two static
// methods to handle the acquire and release operations.
//
// For example:
//
//  struct LockPolicy {
//      struct State {
//          // State members, constructors, and destructors as needed.
//      };
//      static bool Acquire(LockType* lock, State* state) __TA_ACQUIRE(lock) {
//          // Saves any state required by this lock type.
//          // Acquires the lock for this lock type.
//          // Returns whether the acquisition was successful.
//      }
//      static void Release(LockType* lock, State* state) __TA_RELEASE(lock) {
//          // Releases the lock for this lock type.
//          // Restores any state required for this lock type.
//      }
//  };
//
#define LOCK_DEP_POLICY_OPTION(lock_type, option_name, lock_policy)           \
    ::lockdep::AmbiguousOption LOCK_DEP_GetLockPolicyType(lock_type*, void*); \
    lock_policy LOCK_DEP_GetLockPolicyType(lock_type*, option_name*)

// Tags the given lock type with the given policy type. Like the macro above
// the policy type describes how to acquire and release the lock and whether or
// not extra state must be stored to correctly release the lock. This variation
// is appropriate for lock types that do not have different options. This macro
// and the macro above are mutually exclusive and may not be used on the same
// lock type.
//
// Arguments:
//  lock_type : The type of the lock to specify the policy for. This type is
//             passed to the LockType argument of Guard<LockType> to select this
//             lock type to guard. The Optional argument of Guard may not be
//             specified.
//  lock_policy: The policy to use when Guard<LockType> specifies the lock_type
//               given with this policy.
//
// The policy type follows the same structure as the policy type described for
// the macro above.
//
#define LOCK_DEP_POLICY(lock_type, lock_policy) \
    lock_policy LOCK_DEP_GetLockPolicyType(lock_type*, void*)

// Looks up the lock policy for the given lock type and optional option type, as
// specified by the macros above. This utility resolves the tagged types across
// namespaces using ADL.
template <typename Lock, typename Option = void>
using LookupLockPolicy =
    decltype(LOCK_DEP_GetLockPolicyType(
        static_cast<RemoveGlobalReference<Lock>*>(nullptr),
        static_cast<Option*>(nullptr)));

// Default lock policy type that describes how to acquire and release a basic
// mutex with no additional state or flags.
struct DefaultLockPolicy {
    // This policy does not specify any additional state for a lock acquisition.
    struct State {};

    // Acquires the lock by calling its Acquire method. The extra state argument
    // is unused.
    template <typename Lock>
    static bool Acquire(Lock* lock, State*) __TA_ACQUIRE(lock) {
        lock->Acquire();
        return true;
    }

    // Releases the lock by calling its Release method. The extra state argument
    // is unused.
    template <typename Lock>
    static void Release(Lock* lock, State*) __TA_RELEASE(lock) {
        lock->Release();
    }
};

// Sentinel type used to prevent mixing LOCK_DEP_POLICY_OPTION and
// LOCK_DEP_POLICY on same lock type. Mixing these macros on the same lock type
// causes a static assert in Guard for that lock type.
struct AmbiguousOption {};

// Base lock policy type that simply returns the DefaultLockPolicy. This is the
// default policy applied to any lock that is not tagged with the macros above.
template <typename Lock, typename Option = void, typename Enabled = void>
struct LockPolicyType {
    using Type = DefaultLockPolicy;
};

// Specialization that returns the lock policy type for the combination of
// |Lock| and |Option| tagged by the macros above.
template <typename Lock, typename Option>
struct LockPolicyType<Lock, Option, fbl::void_t<LookupLockPolicy<Lock, Option>>> {
    using Type = LookupLockPolicy<Lock, Option>;
};

// Alias that selects the lock policy for the given |Lock| and optional
// |Option| type. This alias simplifies lock policy type expressions used
// elsewhere.
template <typename Lock, typename Option = void>
using LockPolicy = typename LockPolicyType<Lock, Option>::Type;

} // namespace lockdep
