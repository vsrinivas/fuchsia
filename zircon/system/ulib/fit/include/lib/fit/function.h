// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIT_FUNCTION_H_
#define LIB_FIT_FUNCTION_H_

#include <memory>
#include <type_traits>

#include "function_internal.h"
#include "nullable.h"

namespace fit {

template <size_t inline_target_size, bool require_inline,
          typename Result, typename... Args>
class function_impl;

// The default size allowance for storing a target inline within a function
// object, in bytes.  This default allows for inline storage of targets
// as big as two pointers, such as an object pointer and a pointer to a member
// function.
constexpr size_t default_inline_target_size = sizeof(void*) * 2;

// A |fit::function| is a move-only polymorphic function wrapper.
//
// |fit::function<T>| behaves like |std::function<T>| except that it is move-only
// instead of copyable so it can hold targets which cannot be copied, such as
// mutable lambdas.
//
// Targets of up to |inline_target_size| bytes in size (rounded up for memory
// alignment) are stored inline within the function object without incurring
// any heap allocation.  Larger callable objects will be moved to the heap as
// required.
//
// See also |fit::inline_function<T, size>| for more control over allocation
// behavior.
//
// SYNOPSIS
//
// |T| is the function's signature.  e.g. void(int, std::string).
//
// |inline_target_size| is the minimum size of target that is guaranteed to
// fit within a function without requiring heap allocation.
// Defaults to |default_inline_target_size|.
//
// Class members are documented in |fit::function_impl|.
//
// EXAMPLES
//
// - https://fuchsia.googlesource.com/zircon/+/master/system/utest/fit/examples/function_example1.cpp
// - https://fuchsia.googlesource.com/zircon/+/master/system/utest/fit/examples/function_example2.cpp
//
template <typename T, size_t inline_target_size = default_inline_target_size>
using function = function_impl<inline_target_size, false, T>;

// A move-only callable object wrapper which forces callables to be stored inline
// and never performs heap allocation.
//
// Behaves just like |fit::function<T, inline_target_size>| except that attempting
// to store a target larger than |inline_target_size| will fail to compile.
template <typename T, size_t inline_target_size = default_inline_target_size>
using inline_function = function_impl<inline_target_size, true, T>;

// Synonym for a function which takes no arguments and produces no result.
using closure = function<void()>;

// Function implementation details.
// See |fit::function| documentation for more information.
template <size_t inline_target_size, bool require_inline,
          typename Result, typename... Args>
class function_impl<inline_target_size, require_inline, Result(Args...)> final {
    using ops_type = const ::fit::internal::target_ops<Result, Args...>*;
    using storage_type = typename std::aligned_storage<
        (inline_target_size >= sizeof(void*)
             ? inline_target_size
             : sizeof(void*))>::type; // avoid including <algorithm> just for max
    template <typename Callable>
    using target_type = ::fit::internal::target<
        Callable,
        (sizeof(Callable) <= sizeof(storage_type)),
        Result, Args...>;
    using null_target_type = target_type<decltype(nullptr)>;

public:
    // The function's result type.
    using result_type = Result;

    // // Creates a function with an empty target.
    function_impl() {
        initialize_null_target();
    }

    // Creates a function with an empty target.
    function_impl(decltype(nullptr)) {
        initialize_null_target();
    }

    // Creates a function bound to the specified function pointer.
    // If target == nullptr, assigns an empty target.
    function_impl(Result (*target)(Args...)) {
        initialize_target(target);
    }

    // Creates a function bound to the specified callable object.
    // If target == nullptr, assigns an empty target.
    //
    // For functors, we need to capture the raw type but also restrict on the existence of an
    // appropriate operator () to resolve overloads and implicit casts properly.
    template <typename Callable,
              typename = std::enable_if_t<
                  std::is_convertible<
                      decltype(std::declval<Callable&>()(
                          std::declval<Args>()...)),
                      result_type>::value>>
    function_impl(Callable target) {
        initialize_target(std::move(target));
    }

    // Creates a function with a target moved from another function,
    // leaving the other function with an empty target.
    function_impl(function_impl&& other) {
        move_target_from(std::move(other));
    }

    // Destroys the function, releasing its target.
    ~function_impl() {
        destroy_target();
    }

    // Returns true if the function has a non-empty target.
    explicit operator bool() const {
        return ops_ != &null_target_type::ops;
    };

    // Invokes the function's target.
    // Aborts if the function's target is empty.
    Result operator()(Args... args) const {
        return ops_->invoke(&bits_, std::forward<Args>(args)...);
    }

    // Assigns an empty target.
    function_impl& operator=(decltype(nullptr)) {
        destroy_target();
        initialize_null_target();
        return *this;
    }

    // Assigns the function's target.
    // If target == nullptr, assigns an empty target.
    template <typename Callable,
              typename = std::enable_if_t<
                  std::is_convertible<
                      decltype(std::declval<Callable&>()(
                          std::declval<Args>()...)),
                      result_type>::value>>
    function_impl& operator=(Callable target) {
        destroy_target();
        initialize_target(std::move(target));
        return *this;
    }

    // Assigns the function with a target moved from another function,
    // leaving the other function with an empty target.
    function_impl& operator=(function_impl&& other) {
        if (&other == this)
            return *this;
        destroy_target();
        move_target_from(std::move(other));
        return *this;
    }

    // Swaps the functions' targets.
    void swap(function_impl& other) {
        if (&other == this)
            return;
        ops_type temp_ops = ops_;
        storage_type temp_bits;
        ops_->move(&bits_, &temp_bits);

        ops_ = other.ops_;
        other.ops_->move(&other.bits_, &bits_);

        other.ops_ = temp_ops;
        temp_ops->move(&temp_bits, &other.bits_);
    }

    // Returns a pointer to the function's target.
    template <typename Callable>
    Callable* target() {
        check_target_type<Callable>();
        return static_cast<Callable*>(ops_->get(&bits_));
    }

    // Returns a pointer to the function's target.
    template <typename Callable>
    const Callable* target() const {
        check_target_type<Callable>();
        return static_cast<Callable*>(ops_->get(&bits_));
    }

    // Returns a new function object which invokes the same target.
    // The target itself is not copied; it is moved to the heap and its
    // lifetime is extended until all references have been released.
    //
    // Note: This method is not supported on |fit::inline_function<>|
    //       because it may incur a heap allocation which is contrary to
    //       the stated purpose of |fit::inline_function<>|.
    function_impl share() {
        static_assert(!require_inline, "Inline functions cannot be shared.");
        // TODO(jeffbrown): Replace shared_ptr with a better ref-count mechanism.
        // TODO(jeffbrown): This definition breaks the client's ability to use
        // |target()| because the target's type has changed.  We could fix this
        // by defining a new target type (and vtable) for shared targets
        // although it would be nice to avoid memory overhead and code expansion
        // when sharing is not used.
        struct ref {
            std::shared_ptr<function_impl> target;
            Result operator()(Args... args) {
                return (*target)(std::forward<Args>(args)...);
            }
        };
        if (ops_ != &target_type<ref>::ops) {
            if (ops_ == &null_target_type::ops) {
                return nullptr;
            }
            auto target = ref{std::make_shared<function_impl>(std::move(*this))};
            *this = std::move(target);
        }
        return function_impl(*static_cast<ref*>(ops_->get(&bits_)));
    }

    function_impl(const function_impl& other) = delete;
    function_impl& operator=(const function_impl& other) = delete;

private:
    // assumes target is uninitialized
    void initialize_null_target() {
        ops_ = &null_target_type::ops;
    }

    // assumes target is uninitialized
    template <typename Callable>
    void initialize_target(Callable target) {
        static_assert(!require_inline || sizeof(Callable) <= inline_target_size,
                      "Callable too large to store inline as requested.");
        if (is_null(target)) {
            initialize_null_target();
        } else {
            ops_ = &target_type<Callable>::ops;
            target_type<Callable>::initialize(&bits_, std::move(target));
        }
    }

    // leaves target uninitialized
    void destroy_target() {
        ops_->destroy(&bits_);
    }

    // leaves other target initialized to null
    void move_target_from(function_impl&& other) {
        ops_ = other.ops_;
        other.ops_->move(&other.bits_, &bits_);
        other.initialize_null_target();
    }

    template <typename Callable>
    void check_target_type() const {
        if (ops_ != &target_type<Callable>::ops)
            abort();
    }

    ops_type ops_;
    mutable storage_type bits_;
}; // namespace fit

template <size_t inline_target_size, bool require_inline, typename Result, typename... Args>
void swap(function_impl<inline_target_size, require_inline, Result, Args...>& a,
          function_impl<inline_target_size, require_inline, Result, Args...>& b) {
    a.swap(b);
}

template <size_t inline_target_size, bool require_inline, typename Result, typename... Args>
bool operator==(const function_impl<inline_target_size, require_inline, Result, Args...>& f,
                decltype(nullptr)) {
    return !f;
}
template <size_t inline_target_size, bool require_inline, typename Result, typename... Args>
bool operator==(decltype(nullptr),
                const function_impl<inline_target_size, require_inline, Result, Args...>& f) {
    return !f;
}
template <size_t inline_target_size, bool require_inline, typename Result, typename... Args>
bool operator!=(const function_impl<inline_target_size, require_inline, Result, Args...>& f,
                decltype(nullptr)) {
    return !!f;
}
template <size_t inline_target_size, bool require_inline, typename Result, typename... Args>
bool operator!=(decltype(nullptr),
                const function_impl<inline_target_size, require_inline, Result, Args...>& f) {
    return !!f;
}

// Returns a Callable object which invokes a member function of an object.
template <typename R, typename T, typename... Args>
auto bind_member(T* instance, R (T::*fn)(Args...)) {
    return [instance, fn](Args... args) {
        return (instance->*fn)(std::forward<Args>(args)...);
    };
}

} // namespace fit

#endif // LIB_FIT_FUNCTION_H_
