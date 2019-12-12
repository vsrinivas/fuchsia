// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <zircon/assert.h>
#include <zircon/compiler.h>

#include <type_traits>
#include <utility>

#include <lockdep/common.h>
#include <lockdep/lock_class.h>
#include <lockdep/lock_policy.h>
#include <lockdep/lock_traits.h>

namespace lockdep {

namespace internal {

// Utility to deduce the LockType given any subclass of Lock<LockType>.
template <typename LockType>
LockType DeduceLockType(Lock<LockType>*);

// Determines the LockType of any subclass of Lock<LockType> or
// Lock<GlobalReference<LockType, Reference>>.
template <typename T>
using GetLockType = RemoveGlobalReference<decltype(DeduceLockType(static_cast<T*>(nullptr)))>;

// Trait type that determines whether the given LockType is nestable.
template <typename LockType>
struct IsLockTypeNestable : std::bool_constant<(LockTraits<RemoveGlobalReference<LockType>>::Flags &
                                                LockFlagsNestable) != 0> {};

// Trait type that determines whether the given instrumented lock is nestable.
template <typename T>
struct IsLockNestable : std::false_type {};
template <typename Class, typename LockType, size_t Index, LockFlags Flags>
struct IsLockNestable<LockDep<Class, LockType, Index, Flags>>
    : std::bool_constant<(Flags & LockFlagsNestable) != 0> {};

// Trait type that determines whether the given policy type has a nested type
// tag named Shared.
template <typename LockPolicy, typename Enabled = void>
struct IsSharedLockPolicy : std::false_type {};
template <typename LockPolicy>
struct IsSharedLockPolicy<LockPolicy, std::void_t<typename LockPolicy::Shared>> : std::true_type {};

// Detect whether `LockPolicy<LockType>::AssertHeld(const LockType&)` is a valid expression.
template <typename LockType, typename = void>
struct PolicyHasAssertHeld : std::false_type {};
template <typename LockType>
struct PolicyHasAssertHeld<LockType, std::void_t<decltype(LockPolicy<LockType>::AssertHeld(
                                         std::declval<const LockType>()))>> : std::true_type {};

// Enable if the given LockType has a policy supporting `AssertHeld`.
template <typename LockType>
using EnableIfPolicyHasAssertHeld = std::enable_if_t<PolicyHasAssertHeld<LockType>::value>;

// Enable if the given T is nestable and uses same type as LockType.
template <typename T, typename LockType>
using EnableIfNestable =
    std::enable_if_t<std::is_same_v<GetLockType<T>, LockType> &&
                     std::disjunction_v<IsLockTypeNestable<LockType>, IsLockNestable<T>>>;

// Enable if the given T is not nestable and uses same type as LockType.
template <typename T, typename LockType>
using EnableIfNotNestable =
    std::enable_if_t<std::is_same_v<GetLockType<T>, LockType> &&
                     !std::disjunction_v<IsLockTypeNestable<LockType>, IsLockNestable<T>>>;

template <typename LockType, typename Option>
using EnableIfShared = std::enable_if_t<IsSharedLockPolicy<LockPolicy<LockType, Option>>::value>;

template <typename LockType, typename Option>
using EnableIfNotShared =
    std::enable_if_t<!IsSharedLockPolicy<LockPolicy<LockType, Option>>::value>;

}  // namespace internal

// Assert that the given lock is exclusively held by the current thread.
//
// Can be used both for runtime debugging checks, and also to help when
// thread safety analysis can't prove you are holding a lock. The underlying
// lock implementation may optimize away asserts in release builds.
//
// Calling this function requires that LockType has a policy implementing
// `AssertHeld`. The default policy automatically implements AssertHeld
// if the underlying lock object has an `AssertHeld` method.
template <typename Lockable, typename Option = void>
void AssertHeld(const Lockable& lock) __TA_ASSERT(lock) __TA_ASSERT(lock.lock())
    __TA_ASSERT(lock.capability()) {
  LockPolicy<internal::GetLockType<Lockable>, Option>::AssertHeld(lock.lock());
}

// Type tag to select the (private) ordered Guard constructor.
enum OrderedLockTag { OrderedLock };

// Type tag to select the adopting Guard constructor.
enum AdoptLockTag { AdoptLock };

// Base RAII type that automatically manages the duration of a lock acquisition.
// TODO(eieio): Specializations handle exclusive and shared lock acquisitions.
// These are largely identical except for the static lock analysis annotations.
// See if there is away to factor out the common logic for better
// maintainability.
template <typename LockType, typename Option = void, typename Enable = void>
class Guard;

// Specialization of Guard that acquires the given lock in exclusive mode.
template <typename LockType, typename Option>
class __TA_SCOPED_CAPABILITY
    Guard<LockType, Option, internal::EnableIfNotShared<LockType, Option>> {
  static_assert(!std::is_same<LockPolicy<LockType, Option>, AmbiguousOption>::value,
                "The Option argument of Guard<LockType, Option> must always "
                "be specified when the policy for LockType is defined using "
                "the macro LOCK_DEP_POLICY_OPTION(). See the macro docs for "
                "details.");

 public:
  Guard(Guard&&) = delete;
  Guard(const Guard&) = delete;
  Guard& operator=(Guard&&) = delete;
  Guard& operator=(const Guard&) = delete;

  // Acquires the given lock. This constructor participates in overload
  // resolution when the underlying lock type is not nestable.
  template <typename Lockable, typename... Args,
            typename = internal::EnableIfNotNestable<Lockable, LockType>>
  __WARN_UNUSED_CONSTRUCTOR Guard(Lockable* lock, Args&&... state_args) __TA_ACQUIRE(lock)
      __TA_ACQUIRE(lock->capability())
      : validator_{lock->id()}, lock_{&lock->lock()}, state_{std::forward<Args>(state_args)...} {
    ValidateAndAcquire();
  }

  // Acquires the given lock. This constructor participates in overload
  // resolution when the underlying lock type is nestable.
  template <typename Lockable, typename... Args,
            typename = internal::EnableIfNestable<Lockable, LockType>>
  __WARN_UNUSED_CONSTRUCTOR Guard(Lockable* lock, uintptr_t order, Args&&... state_args)
      __TA_ACQUIRE(lock) __TA_ACQUIRE(lock->capability())
      : Guard{OrderedLock, lock, order, std::forward<Args>(state_args)...} {}

  // Destructor that automatically releases the lock if not already released.
  ~Guard() __TA_RELEASE() { Release(); }

  // Releases the lock early before this guard instance goes out of scope.
  template <typename... Args>
  void Release(Args&&... args) __TA_RELEASE() {
    if (lock_ != nullptr) {
      LockPolicy<LockType, Option>::Release(lock_, &state_, std::forward<Args>(args)...);
      validator_.ValidateRelease();
      lock_ = nullptr;
    }
  }

  // Returns true if the guard has an actively acquired lock.
  explicit operator bool() const { return lock_ != nullptr; }
  // Returns true if this guard wraps |lock|.
  bool wraps_lock(const LockType& lock) const { return &lock == lock_; }

  // Releases this scoped capability without releasing the underlying lock or
  // un-tracking the lock in the validator. Returns an rvalue reference to the
  // lock state and validator state which may be adopted by another Guard.
  // This is useful in the rare situation where a lock must be released by a
  // function called in the current protected scope. This is primarily needed
  // to keep the Clang static lock validator happy; static analysis complains
  // when a scoped capability is passed by pointer/reference and released in
  // another scope.
  //
  // Example:
  //  Guard<fbl::Mutex> guard{&lock};
  //
  //  // Setup actions...
  //
  //  DoTaskAndReleaseLock(guard.take());
  //
  Guard&& take() __TA_RELEASE() { return std::move(*this); }

  // Adopts the lock state and validator state. This constructor uses a type
  // tag argument to avoid automatic move constructor semantics.
  //
  // Example:
  //  Guard<fbl::Mutex> guard{AdoptLock, std::move(rvalue_arugment)};
  //
  __WARN_UNUSED_CONSTRUCTOR Guard(AdoptLockTag, Guard&& other) __TA_ACQUIRE(other.lock_)
      : validator_{std::move(other.validator_)},
        lock_{other.lock_},
        state_{std::move(other.state_)} {
    other.lock_ = nullptr;
  }

  // Temporarily releases and un-tracks the guarded lock before executing the
  // given callable Op and then re-acquires and tracks the lock. This permits
  // the same Guard instance to protect a larger scope while performing an
  // operation unlocked. This is especially useful in guarded loops:
  //
  //  Guard<fbl::Mutex> guard{&lock_};
  //  for (auto* entry : objects_.next()) {
  //      if (Pred(entry)) {
  //          objects_.erase(entry);
  //          guard.CallUnlocked([entry]() {
  //              // Unlocked operation on entry ...
  //          });
  //      }
  //  }
  //
  template <typename Op, typename... ReleaseArgs>
  void CallUnlocked(Op&& op, ReleaseArgs&&... release_args) __TA_NO_THREAD_SAFETY_ANALYSIS {
    ZX_DEBUG_ASSERT(lock_ != nullptr);

    LockPolicy<LockType, Option>::Release(lock_, &state_,
                                          std::forward<ReleaseArgs>(release_args)...);
    validator_.ValidateRelease();

    std::forward<Op>(op)();

    ValidateAndAcquire();
  }

 private:
  template <size_t, typename, typename>
  friend class GuardMultiple;

  // Validates and acquires the lock. If the lock is a try-lock that failed
  // the release bookkeeping is performed and the guard is left in the empty
  // state. This method factors out the common body of the main constructors;
  // thread safety analysis is disabled to silence the unnecessary warning
  // about the conditional path that would not be raised in the constructor
  // body.
  void ValidateAndAcquire() __TA_NO_THREAD_SAFETY_ANALYSIS {
    validator_.ValidateAcquire();
    if (!LockPolicy<LockType, Option>::Acquire(lock_, &state_)) {
      lock_ = nullptr;
      validator_.ValidateRelease();
    }
  }

  // Ordered lock constructor used by the nestable lock constructor above and
  // by GuardMultiple.
  template <typename Lockable, typename... Args>
  __WARN_UNUSED_CONSTRUCTOR Guard(OrderedLockTag, Lockable* lock, uintptr_t order,
                                  Args&&... state_args) __TA_ACQUIRE(lock)
      __TA_ACQUIRE(lock->capability())
      : validator_{lock->id(), order},
        lock_{&lock->lock()},
        state_{std::forward<Args>(state_args)...} {
    ValidateAndAcquire();
  }

  // Validator type used when lock validation is enabled. Provides the
  // AcquiredLockEntry instance and bookkeeping calls required by
  // ThreadLockState.
  struct LockValidator {
    LockValidator(LockClassId id, uintptr_t order = 0) : lock_entry{id, order} {}

    void ValidateAcquire() { ThreadLockState::Get()->Acquire(&lock_entry); }
    void ValidateRelease() { ThreadLockState::Get()->Release(&lock_entry); }

    AcquiredLockEntry lock_entry;
  };

  // Validator type used when lock validation is disabled.
  struct DummyValidator {
    DummyValidator(LockClassId, uintptr_t = 0) {}
    void ValidateAcquire() {}
    void ValidateRelease() {}
  };

  // Alias of the configured validator.
  using Validator = IfLockValidationEnabled<LockValidator, DummyValidator>;

  // The validator to use when acquiring and releasing the lock.
  Validator validator_;

  // Pointer to the acquired lock.
  LockType* lock_;

  // State to store in the guard as specified by the lock policy. For example,
  // this may be used to save IRQ state for spinlocks.
  typename LockPolicy<LockType, Option>::State state_;
};

// Specialization of Guard that acquires the given lock in shared mode.
template <typename LockType, typename Option>
class __TA_SCOPED_CAPABILITY Guard<LockType, Option, internal::EnableIfShared<LockType, Option>> {
  static_assert(!std::is_same<LockPolicy<LockType, Option>, AmbiguousOption>::value,
                "The Option argument of Guard<LockType, Option> must always "
                "be specified when the policy for LockType is defined using "
                "the macro LOCK_DEP_POLICY_OPTION(). See the macro docs for "
                "details.");

 public:
  Guard(Guard&&) = delete;
  Guard(const Guard&) = delete;
  Guard& operator=(Guard&&) = delete;
  Guard& operator=(const Guard&) = delete;

  // Acquires the given lock. This constructor participates in overload
  // resolution when the underlying lock type is not nestable.
  template <typename Lockable, typename... Args,
            typename = internal::EnableIfNotNestable<Lockable, LockType>>
  __WARN_UNUSED_CONSTRUCTOR Guard(Lockable* lock, Args&&... state_args) __TA_ACQUIRE_SHARED(lock)
      __TA_ACQUIRE_SHARED(lock->capability())
      : validator_{lock->id()}, lock_{&lock->lock()}, state_{std::forward<Args>(state_args)...} {
    ValidateAndAcquire();
  }

  // Acquires the given lock. This constructor participates in overload
  // resolution when the underlying lock type is nestable.
  template <typename Lockable, typename... Args,
            typename = internal::EnableIfNestable<Lockable, LockType>>
  __WARN_UNUSED_CONSTRUCTOR Guard(Lockable* lock, uintptr_t order, Args&&... state_args)
      __TA_ACQUIRE_SHARED(lock) __TA_ACQUIRE_SHARED(lock->capability())
      : Guard{OrderedLock, lock, order, std::forward<Args>(state_args)...} {}

  // Destructor that automatically releases the lock if not already released.
  ~Guard() __TA_RELEASE() { Release(); }

  // Releases the lock early before this guard instance goes out of scope.
  template <typename... Args>
  void Release(Args&&... args) __TA_RELEASE() {
    if (lock_ != nullptr) {
      LockPolicy<LockType, Option>::Release(lock_, &state_, std::forward<Args>(args)...);
      validator_.ValidateRelease();
      lock_ = nullptr;
    }
  }

  // Returns true if the guard has an actively acquired lock.
  explicit operator bool() const { return lock_ != nullptr; }

  // Releases this scoped capability without releasing the underlying lock or
  // un-tracking the lock in the validator. Returns an rvalue reference to the
  // lock state and validator state which may be adopted by another Guard.
  // This is useful in the rare situation where a lock must be released by a
  // function called in the current protected scope. This is primarily needed
  // to keep the Clang static lock validator happy; static analysis complains
  // when a scoped capability is passed by pointer/reference and released in
  // another scope.
  //
  // Example:
  //  Guard<fbl::Mutex> guard{&lock};
  //
  //  // Setup actions...
  //
  //  DoTaskAndReleaseLock(guard.take());
  //
  Guard&& take() __TA_RELEASE() { return std::move(*this); }

  // Adopts the lock state and validator state. This constructor uses a type
  // tag argument to avoid automatic move constructor semantics.
  //
  // Example:
  //  Guard<fbl::Mutex> guard{AdoptLock, std::move(rvalue_arugment)};
  //
  __WARN_UNUSED_CONSTRUCTOR Guard(AdoptLockTag, Guard&& other) __TA_ACQUIRE_SHARED(other.lock_)
      : validator_{std::move(other.validator_)},
        lock_{other.lock_},
        state_{std::move(other.state_)} {
    other.lock_ = nullptr;
  }

  // Temporarily releases and un-tracks the guarded lock before executing the
  // given callable Op and then re-acquires and tracks the lock. This permits
  // the same Guard instance to protect a larger scope while performing an
  // operation unlocked. This is especially useful in guarded loops:
  //
  //  Guard<fbl::Mutex> guard{&lock_};
  //  for (auto* entry : objects_.next()) {
  //      if (Pred(entry)) {
  //          objects_.erase(entry);
  //          guard.CallUnlocked([entry]() {
  //              // Unlocked operation on entry ...
  //          });
  //      }
  //  }
  //
  template <typename Op, typename... ReleaseArgs>
  void CallUnlocked(Op&& op, ReleaseArgs&&... release_args) __TA_NO_THREAD_SAFETY_ANALYSIS {
    ZX_DEBUG_ASSERT(lock_ != nullptr);

    LockPolicy<LockType, Option>::Release(lock_, &state_,
                                          std::forward<ReleaseArgs>(release_args)...);
    validator_.ValidateRelease();

    std::forward<Op>(op)();

    ValidateAndAcquire();
  }

 private:
  template <size_t, typename, typename>
  friend class GuardMultiple;

  // Validates and acquires the lock. If the lock is a try-lock that failed
  // the release bookkeeping is performed and the guard is left in the empty
  // state. This method factors out the common body of the main constructors;
  // thread safety analysis is disabled to silence the unnecessary warning
  // about the conditional path that would not be raised in the constructor
  // body.
  void ValidateAndAcquire() __TA_NO_THREAD_SAFETY_ANALYSIS {
    validator_.ValidateAcquire();
    if (!LockPolicy<LockType, Option>::Acquire(lock_, &state_)) {
      lock_ = nullptr;
      validator_.ValidateRelease();
    }
  }

  // Ordered lock constructor used by the nestable lock constructor above and
  // by GuardMultiple.
  template <typename Lockable, typename... Args>
  __WARN_UNUSED_CONSTRUCTOR Guard(OrderedLockTag, Lockable* lock, uintptr_t order,
                                  Args&&... state_args) __TA_ACQUIRE_SHARED(lock)
      __TA_ACQUIRE_SHARED(lock->capability())
      : validator_{lock->id(), order},
        lock_{&lock->lock()},
        state_{std::forward<Args>(state_args)...} {
    ValidateAndAcquire();
  }

  // Validator type used when lock validation is enabled. Provides the
  // AcquiredLockEntry instance and bookkeeping calls required by
  // ThreadLockState.
  struct LockValidator {
    LockValidator(LockClassId id, uintptr_t order = 0) : lock_entry{id, order} {}

    void ValidateAcquire() { ThreadLockState::Get()->Acquire(&lock_entry); }
    void ValidateRelease() { ThreadLockState::Get()->Release(&lock_entry); }

    AcquiredLockEntry lock_entry;
  };

  // Validator type used when lock validation is disabled.
  struct DummyValidator {
    DummyValidator(LockClassId, uintptr_t = 0) {}
    void ValidateAcquire() {}
    void ValidateRelease() {}
  };

  // Alias of the configured validator.
  using Validator = IfLockValidationEnabled<LockValidator, DummyValidator>;

  // The validator to use when acquiring and releasing the lock.
  Validator validator_;

  // Pointer to the acquired lock.
  LockType* lock_;

  // State to store in the guard as specified by the lock policy. For example,
  // this may be used to save IRQ state for spinlocks.
  typename LockPolicy<LockType, Option>::State state_;
};

}  // namespace lockdep
