// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <zircon/assert.h>

#include <atomic>
#include <cstddef>
#include <new>
#include <type_traits>
#include <utility>

namespace lazy_init {

// Enum that specifies what kind of debug init checks to perform for a
// lazy-initialized global variable.
enum class CheckType {
    // No checks are performed.
    None,

    // Initalization checks are performed. If multiple threads will access the
    // global variable, initialization must be manually serialized with respect
    // to the guard variable.
    Basic,

    // Initialization checks are performed using atomic operations. Checks are
    // guaranteed to be consistent, even when races occur over initialization.
    Atomic,

    // The default check type as specified by the build. This is the check type
    // used when not explicitly specified. It may also be specified explicitly
    // to defer to the build configuration when setting other options.
    // TODO(eieio): Add the build arg and conditional logic.
    Default = None,
};

// Enum that specifies whether to enable a lazy-initialized global variable's
// destructor. Disabling global destructors avoids destructor registration.
// However, destructors can be conditionally enabled on builds that require
// them, such as ASAN.
enum class Destructor {
    Disabled,
    Enabled,

    // The default destructor enablement as specified by the build. This is the
    // enablement used when not explicitly specified. It may also be specified
    // explicitly to defer to the build configuration when setting other
    // options.
    // TODO(eieio): Add the build arg and conditional logic.
    Default = Disabled,
};

namespace internal {

// Empty type that is trivially constructible/destructible.
struct Empty {};

// Lazy-initialized storage type for trivially destructible value types.
template <typename T, bool = std::is_trivially_destructible_v<T>>
union LazyInitStorage {
    constexpr LazyInitStorage()
        : empty{} {}

    // Trivial destructor required so that the overall union is also trivially
    // destructible.
    ~LazyInitStorage() = default;

    constexpr T& operator*() {
        return value;
    }
    constexpr T* operator->() {
        return &value;
    }
    constexpr T* operator&() {
        return &value;
    }

    Empty empty;
    T value;
};

// Lazy-initialized storage type for non-trivially destructible value types.
template <typename T>
union LazyInitStorage<T, false> {
    constexpr LazyInitStorage()
        : empty{} {}

    // Non-trivial destructor required when at least one variant is non-
    // trivially destructible, making the overall union also non-trivially
    // destructible.
    ~LazyInitStorage() {}

    constexpr T& operator*() {
        return value;
    }
    constexpr T* operator->() {
        return &value;
    }
    constexpr T* operator&() {
        return &value;
    }

    Empty empty;
    T value;
};

} // namespace internal

// Wrapper type for global variables that removes automatic constructor and
// destructor generation and provides explicit control over initialization.
// This avoids initialization order hazards between globals, as well as code
// that runs before and after global constructors are invoked.
//
// This is the base type that specifies the default parameters that control
// consistency checks and global destructor generation options.
template <typename T,
          CheckType = CheckType::Default,
          Destructor = Destructor::Default>
class LazyInit;

// Specialization that does not provide consistency checks.
template <typename T>
class LazyInit<T, CheckType::None, Destructor::Disabled> {
public:
    constexpr LazyInit() = default;
    ~LazyInit() = default;

    LazyInit(const LazyInit&) = delete;
    LazyInit& operator=(const LazyInit&) = delete;
    LazyInit(LazyInit&&) = delete;
    LazyInit& operator=(LazyInit&&) = delete;

    // Explicitly constructs the wrapped global. It is up to the caller to
    // ensure that initialization is not already performed.
    // Returns a reference to the newly constructed global.
    template <typename... Args>
    std::enable_if_t<std::is_constructible_v<T, Args&&...>, T&>
    Initialize(Args&&... args) {
        static_assert(alignof(LazyInit) >= alignof(T));

        new (&storage_) T(std::forward<Args>(args)...);
        return *storage_;
    }

    // Returns a reference to the wrapped global. It is up to the caller to
    // ensure that initialization is already performed and that the effects of
    // initialization are visible.
    T& Get() {
        return *storage_;
    }

    // Accesses the wrapped global by pointer. It is up to the caller to ensure
    // that initialization is already performed and that the effects of
    // initialization are visible.
    T* operator->() {
        return &Get();
    }

    // Returns a pointer to the wrapped global. All specializations return a
    // pointer without performing consistency checks. This should be used
    // cautiously, preferably only in constant expressions that take the address
    // of the wrapped global.
    constexpr T* operator&() {
        return &storage_;
    }

private:
    template <typename, CheckType, Destructor>
    friend class LazyInit;

    // Explicitly destroys the wrapped global. Called by the specialization of
    // LazyInit with the destructor enabled.
    void Destruct() {
        storage_->T::~T();
    }

    // Lazy-initialized storage for a value of type T.
    internal::LazyInitStorage<T> storage_;
};

// Specialization that provides basic consistency checks. It is up to the caller
// to ensure proper synchronization and barriers.
template <typename T>
class LazyInit<T, CheckType::Basic, Destructor::Disabled> {
public:
    constexpr LazyInit() = default;
    ~LazyInit() = default;

    LazyInit(const LazyInit&) = delete;
    LazyInit& operator=(const LazyInit&) = delete;
    LazyInit(LazyInit&&) = delete;
    LazyInit& operator=(LazyInit&&) = delete;

    // Explicitly constructs the wrapped global. Asserts that initialization is
    // not already performed.
    // Returns a reference to the newly constructed global.
    template <typename... Args>
    std::enable_if_t<std::is_constructible_v<T, Args&&...>, T&>
    Initialize(Args&&... args) {
        static_assert(alignof(LazyInit) >= alignof(T));

        ZX_ASSERT(!*initialized_);
        *initialized_ = true;
        new (&storage_) T(std::forward<Args>(args)...);

        return *storage_;
    }

    // Returns a reference to the wrapped global. Asserts that initialization
    // is already performed, however, it is up to the caller to ensure that the
    // effects of initialization are visible.
    T& Get() {
        ZX_ASSERT(*initialized_);
        return *storage_;
    }

    // Accesses the wrapped global by pointer. Asserts that initialization is
    // already perfromed, however, it is up the caller to ensure that the
    // effects of initialization are visible.
    T* operator->() {
        return &Get();
    }

    // Returns a pointer to the wrapped global. All specializations return a
    // pointer without performing consistency checks. This should be used
    // cautiously, preferably only in constant expressions that take the address
    // of the wrapped global.
    constexpr T* operator&() {
        return &storage_;
    }

private:
    template <typename, CheckType, Destructor>
    friend class LazyInit;

    // Explicitly destroys the wrapped global. Called by the specialization of
    // LazyInit with the destructor enabled.
    void Destruct() {
        ZX_ASSERT(*initialized_);
        storage_->T::~T();

        // Prevent the compiler from omitting this write during destruction.
        static_cast<volatile bool&>(*initialized_) = false;
    }

    // Lazy-initialized storage for a value of type T.
    internal::LazyInitStorage<T> storage_;

    // Guard variable to check for multiple initializations and access before
    // initialization.
    internal::LazyInitStorage<bool> initialized_;
};

// Specialization that provides atomic consistency checks. Checks are guaranteed
// to be consistent under races over initialization.
template <typename T>
class LazyInit<T, CheckType::Atomic, Destructor::Disabled> {
public:
    constexpr LazyInit() = default;
    ~LazyInit() = default;

    LazyInit(const LazyInit&) = delete;
    LazyInit& operator=(const LazyInit&) = delete;
    LazyInit(LazyInit&&) = delete;
    LazyInit& operator=(LazyInit&&) = delete;

    // Explicitly constructs the wrapped global. Asserts that initialization is
    // not already performed.
    // Returns a reference to the newly constructed global.
    template <typename... Args>
    std::enable_if_t<std::is_constructible_v<T, Args&&...>, T&>
    Initialize(Args&&... args) {
        static_assert(alignof(LazyInit) >= alignof(T));

        TransitionState(State::Uninitialized, State::Constructing);
        new (&storage_) T(std::forward<Args>(args)...);
        state_->store(State::Initialized, std::memory_order_release);

        return *storage_;
    }

    // Returns a reference to the wrapped global. Asserts that initialization
    // is already performed. The effects of initialization are guaranteed to be
    // visible if the assertion passes.
    T& Get() {
        AssertState(State::Initialized, state_->load(std::memory_order_relaxed));
        return *storage_;
    }

    // Accesses the wrapped global by pointer. Asserts that initialization is
    // already perfromed. The effects of initialization are guaranteed to be
    // visible if the assertion passes.
    T* operator->() {
        return &Get();
    }

    // Returns a pointer to the wrapped global. All specializations return a
    // pointer without performing consistency checks. This should be used
    // cautiously, preferably only in constant expressions that take the address
    // of the wrapped global.
    constexpr T* operator&() {
        return &storage_;
    }

private:
    template <typename, CheckType, Destructor>
    friend class LazyInit;

    // Enum representing the states of initialization.
    enum class State : int {
        Uninitialized = 0,
        Constructing,
        Initialized,
        Destructing,
        Destroyed,
    };

    // Asserts that the expected state matches the actual state.
    void AssertState(State expected, State actual) {
        ZX_ASSERT_MSG(expected == actual, "expected=%d actual=%d",
                      static_cast<int>(expected), static_cast<int>(actual));
    }

    // Transitions the guard from the |expected| state to the |target| state.
    // Asserts that the expected state matches the actual state.
    void TransitionState(State expected, State target) {
        State actual = state_->load(std::memory_order_relaxed);
        AssertState(expected, actual);
        while (!state_->compare_exchange_weak(actual, target,
                                              std::memory_order_acquire,
                                              std::memory_order_relaxed)) {
            AssertState(expected, actual);
        }
    }

    // Explicitly destroys the wrapped global. Called by the specialization of
    // LazyInit with the destructor enabled.
    void Destruct() {
        TransitionState(State::Initialized, State::Destructing);
        storage_->T::~T();
        state_->store(State::Destroyed, std::memory_order_release);
    }

    // Lazy-initialized storage for a value of type T.
    internal::LazyInitStorage<T> storage_;

    // Guard variable to check for multiple initializations and access before
    // initialization.
    internal::LazyInitStorage<std::atomic<State>> state_;
};

// Specialization that includes a global destructor. This type is based on the
// equivalent specialization without a global destructor and simply calls the
// base type Destruct() method.
template <typename T, CheckType Check>
class LazyInit<T, Check, Destructor::Enabled>
    : public LazyInit<T, Check, Destructor::Disabled> {
public:
    using LazyInit<T, Check, Destructor::Disabled>::LazyInit;
    ~LazyInit() {
        this->Destruct();
    }
};

} // namespace lazy_init
