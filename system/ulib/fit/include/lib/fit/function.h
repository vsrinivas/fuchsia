// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIT_FUNCTION_H_
#define LIB_FIT_FUNCTION_H_

#include "function_internal.h"

namespace fit {

// The default size allowance for storing a target inline within a function
// object, in bytes.  This default allows for inline storage of targets
// as big as two pointers, such as an object pointer and a pointer to a member
// function.
constexpr size_t default_inline_target_size = sizeof(void*) * 2;

// A move-only polymorphic function wrapper.
//
// fit::function<T> behaves like std::function<T> except that it is move-only
// instead of copyable so it can hold targets which cannot be copied, such as
// mutable lambdas.
//
// Targets of up to |inline_target_size| bytes in size (rounded up for memory
// alignment) are stored inline within the function object without incurring
// any heap allocation.  Larger callable objects will be moved to the heap as
// required.
//
// See also fit::inline_function<T, size> for more control over allocation
// behavior.
//
// SYNOPSIS:
//
// template <typename T, size_t inline_target_size = default_inline_target_size>
// class function {
// public:
//     // The function's result type.
//     using result_type = Result;
//
//     // Creates a function with an empty target.
//     function();
//
//     // Creates a function with an empty target.
//     explicit function(decltype(nullptr));
//
//     // Creates a function bound to the specified target.
//     // If target == nullptr, assigns an empty target.
//     template <typename Callable>
//     explicit function(Callable target);
//
//     // Creates a function with a target moved from another function,
//     // leaving the other function with an empty target.
//     function(function&& other);
//
//     // Destroys the function, releasing its target.
//     ~function();
//
//     // Returns true if the function has a non-empty target.
//     explicit operator bool() const;
//
//     // Invokes the function's target.
//     // Aborts if the function's target is empty.
//     Result operator()(Args... args) const;
//
//     // Assigns an empty target.
//     function& operator=(decltype(nullptr));
//
//     // Assigns the function's target.
//     // If target == nullptr, assigns an empty target.
//     template <typename Callable>
//     function& operator=(Callable target);
//
//     // Assigns the function with a target moved from another function,
//     // leaving the other function with an empty target.
//     function& operator=(function&& other);
//
//     // Swaps the functions' targets.
//     void swap(function& other);
//
//     // Returns a pointer to the target.
//     template <typename Callable>
//     Callable* target();
//     template <typename Callable>
//     const Callable* target() const;
//
//     // Returns a new function object which invokes the same target.
//     // The target itself is not copied; it is moved to the heap and its
//     // lifetime is extended until all references have been released.
//     // Note: This method is not supported on fit::inline_function<>
//     //       because it may incur heap allocation.
//     function share();
// };
//
// template <typename T, size_t inline_target_size>
// bool operator==(const function<T> f, decltype(nullptr));
//
// template <typename T, size_t inline_target_size>
// bool operator==(decltype(nullptr), const function<T> f);
//
// template <typename T, size_t inline_target_size>
// bool operator!=(const function<T> f, decltype(nullptr));
//
// template <typename T, size_t inline_target_size>
// bool operator!=(decltype(nullptr), const function<T> f);
//
// EXAMPLE:
//
// using fold_function = fit::function<int(int value, int item)>;
//
// int fold(const std::vector<int>& in, int value, const fold_function& f) {
//     for (auto& item : in) {
//         value = f(value, item);
//     }
//     return value;
// }
//
// int sum_item(int value, int item) {
//     return value + item;
// }
//
// int sum(const std::vector<int>& in) {
//     // bind to a function pointer
//     fold_function fn(&sum_item);
//     return fold(in, 0, fn);
// }
//
// int alternating_sum(const std::vector<int>& in) {
//     // bind to a lambda
//     int sign = 1;
//     fold_function fn([&sign](int value, int item) {
//         value += sign * item;
//         sign *= -1;
//         return value;
//     });
//     return fold(in, 0, fn);
// }
//
template <typename T, size_t inline_target_size = default_inline_target_size>
using function = function_impl<inline_target_size, false, T>;

// A move-only callable object wrapper which forces callables to be stored inline
// and never performs heap allocation.
//
// Behaves just like fit::function<T, inline_target_size> except that attempting
// to store a target larger than |inline_target_size| will fail to compile.
template <typename T, size_t inline_target_size = default_inline_target_size>
using inline_function = function_impl<inline_target_size, true, T>;

// Synonym for a function which takes no arguments and produces no result.
using closure = function<void()>;

// Returns a Callable object which invokes a member function of an object.
//
// EXAMPLE:
//
// class accumulator {
// public:
//     void add(int value) {
//          sum += value;
//     }
//
//     int sum = 0;
// };
//
// void count_to_ten(fit::function<void(int)> fn) {
//     for (int i = 1; i <= 10; i++) {
//         fn(i);
//     }
// }
//
// int sum_to_ten() {
//     accumulator accum;
//     count_to_ten(fit::bind_member(&accum, &accumulator::add));
//     return accum.sum;
// }
template <typename R, typename T, typename... Args>
auto bind_member(T* instance, R (T::*fn)(Args...)) {
    return [instance, fn](Args... args) {
        return (instance->*fn)(std::forward<Args>(args)...);
    };
}

} // namespace fit

#endif // LIB_FIT_FUNCTION_H_
