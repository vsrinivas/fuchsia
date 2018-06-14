// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <zircon/compiler.h>

#include <fbl/type_info.h>
#include <fbl/type_support.h>

#include <lockdep/common.h>
#include <lockdep/global_reference.h>
#include <lockdep/lock_class_state.h>
#include <lockdep/lock_traits.h>
#include <lockdep/thread_lock_state.h>

namespace lockdep {

// Looks up a lock traits type using ADL to perfrom cross-namespace matching.
// This alias works in concert with the macro LOCK_DEP_TRAITS(type, flags) to
// find the defined lock traits for a lock, even when defined in a different
// namespace.
template <typename T>
using LookupLockTraits =
    decltype(LOCK_DEP_GetLockTraits(static_cast<T*>(nullptr)));

// Base lock traits type. If a type has not been tagged with the
// LOCK_DEP_TRAITS() then this base type provides the default flags for the lock
// type.
template <typename T, typename = void>
struct LockTraits {
    static constexpr LockFlags Flags = LockFlagsNone;
};
// Returns the flags for a lock type when the type is tagged with
// LOCK_DEP_TRAITS(type, flags).
template <typename T>
struct LockTraits<T, fbl::void_t<LookupLockTraits<T>>> {
    static constexpr LockFlags Flags = LookupLockTraits<T>::Flags;
};

// Singleton type that represents a lock class. Each template instantion
// represents an independent, unique lock class. This type maintains a global
// dependency set that tracks which other lock classes have been observed
// being held prior to acquisitions of this lock class. This type is only used
// when lock validation is enabled, otherwise DummyLockClass takes its place.
template <typename Class, typename LockType, size_t Index, LockFlags Flags>
class LockClass {
public:
    // Returns the unique lock class id for this lock class.
    static LockClassId Id() {
        return lock_class_state_.id();
    }

    // Returns the LockClassState instance for this lock class.
    static LockClassState* GetLockClassState() { return &lock_class_state_; }

private:
    LockClass() = default;
    ~LockClass() = default;
    LockClass(const LockClass&) = delete;
    void operator=(const LockClass&) = delete;

    // Returns the name of this lock class.
    constexpr static const char* GetName() {
        return fbl::TypeInfo<LockClass>::Name();
    }

    // Lock free dependency set that tracks which lock classes have been observed
    // being held prior to acquisitions of this lock class.
    static LockDependencySet dependency_set_;

    // This static member serves a dual role:
    //  1. Stores the id, name, and pointer to dependency set for this lock class.
    //  2. The address of this member serves as the unique id for this lock class.
    static LockClassState lock_class_state_;
};

// Initializes the value of the lock class state with the id, typeid name, and
// dependency set of the lock class. This results in a global initializer that
// performs trivial assignment.
template <typename Class, typename LockType, size_t Index, LockFlags Flags>
LockClassState LockClass<Class, LockType, Index, Flags>::lock_class_state_ = {
    LockClass<Class, LockType, Index, Flags>::GetName(),
    &LockClass<Class, LockType, Index, Flags>::dependency_set_,
    static_cast<LockFlags>(LockTraits<LockType>::Flags | Flags)};

// The storage for this static member is allocated as zeroed memory.
template <typename Class, typename LockType, size_t Index, LockFlags Flags>
LockDependencySet LockClass<Class, LockType, Index, Flags>::dependency_set_;

// Dummy type used in place of LockClass when validation is disabled. This type
// does not create static dependency tracking structures that LockClass does.
struct DummyLockClass {
    static LockClassId Id() { return kInvalidLockClassId; }
};

// Alias that selects LockClass<Class, LockType, Index, Flags> when validation
// is enabled or DummyLockClass when validation is disabled.
template <typename Class, typename LockType, size_t Index, LockFlags Flags>
using ConditionalLockClass = IfLockValidationEnabled<
    LockClass<Class, LockType, Index, Flags>, DummyLockClass>;

// Base lock wrapper type that provides the essential interface required by
// Guard<LockType, Option> to perform locking and validation. This type wraps
// an instance of LockType that is used to perform the actual synchronization.
// When lock validation is enabled this type also stores the LockClassId for
// the lock class this lock belongs to.
//
// The "lock class" that each lock belongs to is created by each unique
// instantiation of the types LockDep<Class, LockType, Index> or
// SingletonLockDep<Class, LockType, LockFlags> below. These types subclass
// Lock<LockType> to provide type erasure required when virtual accessors are
// used to specify capabilities to Clang static lock analysis.
//
// For example, the lock_ members of LockableType and AnotherLockableType below
// are different types due to how LockDep<> instruments the lock members.
// Both unique LockDep<> instantiations derive from the same Lock<LockType>,
// providing a common capability type (erasing the LockDep<> type) that may be
// used in static lock annotations with the expression "get_lock()".
//
//  struct LockableInterface {
//      virtual ~LockableInterface = 0;
//      virtual Lock<fbl::Mutex>* get_lock() = 0;
//      virtual void Clear() __TA_REQUIRES(get_lock()) = 0;
//  };
//
//  class LockableType : public LockableInterface {
//  public:
//      LockableType() = default;
//      ~LockableType() override = default;
//
//      Lock<fbl::Mutex>* get_lock() override { return &lock_; }
//
//      void Clear() override { count_ = 0; }
//
//      void Increment() {
//          Guard<fbl::Mutex> guard{get_lock()};
//          count_++;
//      }
//
//
//  private:
//      LOCK_DEP_INSTRUMENT(LockableType, fbl::Mutex) lock_;
//      int count_ __TA_GUARDED(get_lock()) {0};
//  };
//
//  class AnotherLockableType : public LockableInterface {
//  public:
//      AnotherLockableType() = default;
//      ~AnotherLockableType() override = default;
//
//      Lock<fbl::Mutex>* get_lock() override { return &lock_; }
//
//      void Clear() override { test_.clear(); }
//
//      void Append(const std::string& string) {
//          Guard<fbl::Mutex> guard{get_lock()};
//          text_.append(string)
//      }
//
//
//  private:
//      LOCK_DEP_INSTRUMENT(AnotherLockableType, fbl::Mutex) lock_;
//      std::string text_ __TA_GUARDED(get_lock()) {};
//  };
//
template <typename LockType>
class __TA_CAPABILITY("mutex") Lock {
public:
    Lock(Lock&&) = delete;
    Lock(const Lock&) = delete;
    Lock& operator=(Lock&&) = delete;
    Lock& operator=(const Lock&) = delete;

    ~Lock() = default;

    // Provides direct access to the underlying lock. Care should be taken when
    // manipulating the underlying lock. Incorrect manipulation could confuse
    // the validator, trigger lock assertions, and/or deadlock.
    LockType& lock() { return lock_; }

    // Returns the capability of the underlying lock. This is expected by Guard
    // as an additional static assertion target.
    LockType& capability() __TA_RETURN_CAPABILITY(lock_) { return lock_; }

protected:
    // Initializes the Lock instance with the given LockClassId and passes any
    // additional arguments to the underlying lock constructor.
    template <typename... Args>
    constexpr Lock(LockClassId id, Args&&... args)
        : id_{id}, lock_(fbl::forward<Args>(args)...) {}

private:
    template <typename, typename>
    friend class Guard;
    template <size_t, typename, typename>
    friend class GuardMultiple;

    // Returns the LockClassId of the lock class this lock belongs to.
    LockClassId id() const { return id_.value(); }

    // Value type that stores the LockClassId for this lock when validation is
    // enabled.
    struct Value {
        LockClassId value_;
        LockClassId value() const { return value_; }
    };

    // Dummy type that stores nothing when validation is disabled.
    struct Dummy {
        Dummy(LockClassId) {}
        LockClassId value() const { return kInvalidLockClassId; }
    };

    // Selects between Value or Dummy based on whether validation is enabled.
    using IdValue = IfLockValidationEnabled<Value, Dummy>;

    // Stores the lock class id of this lock when validation is enabled.
    IdValue id_;

    // The underlying lock managed by this dependency tracking wrapper.
    LockType lock_;
};

// Specialization of Lock<LockType> that wraps a static/global raw lock. This
// type permits creating a tracked alias of a raw lock of type LockType that
// has static storage duration, with either external or internal linkage.
//
// This type supports tranistioning from C-compatible APIs to full C++.
template <typename LockType, LockType& Reference>
class __TA_CAPABILITY("mutex") Lock<GlobalReference<LockType, Reference>> {
public:
    Lock(Lock&&) = delete;
    Lock(const Lock&) = delete;
    Lock& operator=(Lock&&) = delete;
    Lock& operator=(const Lock&) = delete;

    ~Lock() = default;

    LockType& lock() { return Reference; }

protected:
    constexpr Lock(LockClassId id)
        : id_{id} {}

private:
    template <typename, typename>
    friend class Guard;
    template <size_t, typename, typename>
    friend class GuardMultiple;

    LockClassId id() const { return id_.value(); }

    struct Value {
        LockClassId value_;
        LockClassId value() const { return value_; }
    };
    struct Dummy {
        Dummy(LockClassId) {}
        LockClassId value() const { return kInvalidLockClassId; }
    };

    using IdValue = IfLockValidationEnabled<Value, Dummy>;

    // Stores the lock class id of this lock when validation is enabled.
    IdValue id_;
};

// Utility type that captures a LockFlags bitmask in the type system. This may
// be used to pass extra LockFlags to the LockDep<> constructor.
template <LockFlags Flags = LockFlagsNone>
struct ExtraFlags {};

// Lock wrapper class that implements lock dependency checks. The template
// argument |Class| should be a type that uniquely defines the class, such as
// the type of the containing scope. The template argument |LockType| is the
// type of the lock to wrap. The template argument |Index| may be used to
// differentiate lock classes between multiple locks within the same scope.
//
// For example:
//
//  struct MyType {
//      LockDep<MyType, Mutex, 0> lock_a;
//      LockDep<MyType, Mutex, 1> lock_b;
//      // ...
//  };
//
template <typename Class, typename LockType, size_t Index = 0>
class LockDep : public Lock<LockType> {
public:
    // Alias that may be used by subclasses to simplify constructor
    // expressions.
    using Base = LockDep;

    // Alias of the lock class that this wrapper represents.
    template <LockFlags Flags = LockFlagsNone>
    using LockClass = ConditionalLockClass<Class,
                                           RemoveGlobalReference<LockType>,
                                           Index, Flags>;

    // Constructor that initializes the underlying lock with the additional
    // arguments.
    template <typename... Args>
    constexpr LockDep(Args&&... args)
        : Lock<LockType>{LockClass<>::Id(), fbl::forward<Args>(args)...} {}

    // Constructor that accepts additional LockFlags to apply to the lock class
    // for this lock. The additional arguments are passed to the underlying
    // lock.
    template <LockFlags Flags, typename... Args>
    constexpr LockDep(ExtraFlags<Flags>, Args&&... args)
        : Lock<LockType>{LockClass<Flags>::Id(), fbl::forward<Args>(args)...} {}
};

// Singleton version of the lock wrapper above. This type is appropriate for
// global locks. This type is used by the macros LOCK_DEP_SINGLETON_LOCK and
// LOCK_DEP_SINGLETON_LOCK_WRAPPER to define instrumented global locks.
template <typename Class, typename LockType, LockFlags Flags = LockFlagsNone>
class SingletonLockDep : public Lock<LockType> {
public:
    // Alias of the lock class that this wrapper represents.
    using LockClass = ConditionalLockClass<Class,
                                           RemoveGlobalReference<LockType>,
                                           0, Flags>;

    // Returns a pointer to the singleton object.
    static Class* Get() {
        // The singleton instance of the global lock.
        static Class global_lock;

        return &global_lock;
    }

protected:
    // Initializes the base Lock<LockType> with lock class id for this lock. The
    // additional arguments are pass to the underlying lock.
    template <typename... Args>
    constexpr SingletonLockDep(Args&&... args)
        : Lock<LockType>{LockClass::Id(), fbl::forward<Args>(args)...} {}
};

} // namespace lockdep
