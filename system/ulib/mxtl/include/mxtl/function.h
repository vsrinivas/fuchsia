// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stddef.h>

#include <magenta/assert.h>
#include <mxtl/algorithm.h>
#include <mxtl/alloc_checker.h>
#include <mxtl/macros.h>
#include <mxtl/new.h>
#include <mxtl/type_support.h>
#include <mxtl/unique_ptr.h>

namespace mxtl {
namespace internal {

template <typename Result, typename... Args>
class FunctionTarget {
public:
    FunctionTarget() = default;
    virtual ~FunctionTarget() = default;

    DISALLOW_COPY_ASSIGN_AND_MOVE(FunctionTarget);

    virtual bool is_null() const = 0;

    virtual Result operator()(Args... args) const = 0;

    virtual void MoveInitializeTo(void* ptr) = 0;
};

template <typename Result, typename... Args>
class NullFunctionTarget final : public FunctionTarget<Result, Args...> {
public:
    NullFunctionTarget() = default;
    ~NullFunctionTarget() final = default;

    DISALLOW_COPY_ASSIGN_AND_MOVE(NullFunctionTarget);

    bool is_null() const final { return true; }

    Result operator()(Args... args) const final {
        MX_PANIC("Attempted to invoke mxtl::Function with a null target.");
    }

    void MoveInitializeTo(void* ptr) final {
        new (ptr) NullFunctionTarget();
    }
};

template <typename Callable, typename Result, typename... Args>
class InlineFunctionTarget final : public FunctionTarget<Result, Args...> {
public:
    explicit InlineFunctionTarget(Callable target)
        : target_(mxtl::move(target)) {}
    InlineFunctionTarget(Callable target, AllocChecker* ac)
        : target_(mxtl::move(target)) { ac->arm(0u, true); }
    InlineFunctionTarget(InlineFunctionTarget&& other)
        : target_(mxtl::move(other.target_)) {}
    ~InlineFunctionTarget() final = default;

    DISALLOW_COPY_AND_ASSIGN_ALLOW_MOVE(InlineFunctionTarget);

    bool is_null() const final { return false; }

    Result operator()(Args... args) const final {
        return target_(mxtl::forward<Args>(args)...);
    }

    void MoveInitializeTo(void* ptr) final {
        new (ptr) InlineFunctionTarget(mxtl::move(*this));
    }

private:
    mutable Callable target_;
};

template <typename Callable, typename Result, typename... Args>
class HeapFunctionTarget final : public FunctionTarget<Result, Args...> {
public:
    explicit HeapFunctionTarget(Callable target)
        : target_ptr_(mxtl::make_unique<Callable>(mxtl::move(target))) {}
    HeapFunctionTarget(Callable target, AllocChecker* ac)
        : target_ptr_(mxtl::make_unique_checked<Callable>(ac, mxtl::move(target))) {}
    HeapFunctionTarget(HeapFunctionTarget&& other)
        : target_ptr_(mxtl::move(other.target_ptr_)) {}
    ~HeapFunctionTarget() final = default;

    DISALLOW_COPY_AND_ASSIGN_ALLOW_MOVE(HeapFunctionTarget);

    bool is_null() const final { return false; }

    Result operator()(Args... args) const final {
        return (*target_ptr_)(mxtl::forward<Args>(args)...);
    }

    void MoveInitializeTo(void* ptr) final {
        new (ptr) HeapFunctionTarget(mxtl::move(*this));
    }

private:
    mxtl::unique_ptr<Callable> target_ptr_;
};

// Holds a function target.
// If a callable object is small enough, it will be stored as an |InlineFunctionTarget|.
// Otherwise it will be stored as a |HeapFunctionTarget|.
template <size_t target_size, typename Result, typename... Args>
struct FunctionTargetHolder final {
    FunctionTargetHolder() = default;

    DISALLOW_COPY_ASSIGN_AND_MOVE(FunctionTargetHolder);

    void InitializeNullTarget() {
        using NullFunctionTarget = mxtl::internal::NullFunctionTarget<Result, Args...>;
        static_assert(sizeof(NullFunctionTarget) <= target_size,
                      "NullFunctionTarget should fit in FunctionTargetHolder.");
        new (&bits_) NullFunctionTarget();
    }

    template <typename Callable>
    struct TargetHelper {
        using InlineFunctionTarget = mxtl::internal::InlineFunctionTarget<Callable, Result, Args...>;
        using HeapFunctionTarget = mxtl::internal::HeapFunctionTarget<Callable, Result, Args...>;
        static constexpr bool can_inline = (sizeof(InlineFunctionTarget) <= target_size);
        using Type = typename mxtl::conditional<can_inline, InlineFunctionTarget, HeapFunctionTarget>::type;
        static_assert(sizeof(Type) <= target_size, "Target should fit in FunctionTargetHolder.");
    };

    template <typename Callable>
    void InitializeTarget(Callable target) {
        new (&bits_) typename TargetHelper<Callable>::Type(mxtl::move(target));
    }

    template <typename Callable>
    void InitializeTarget(Callable target, AllocChecker* ac) {
        new (&bits_) typename TargetHelper<Callable>::Type(mxtl::move(target), ac);
    }

    void MoveInitializeTargetFrom(FunctionTargetHolder& other) {
        other.target().MoveInitializeTo(&bits_);
    }

    void DestroyTarget() {
        target().~FunctionTarget();
    }

    using FunctionTarget = mxtl::internal::FunctionTarget<Result, Args...>;
    FunctionTarget& target() { return *reinterpret_cast<FunctionTarget*>(&bits_); }
    const FunctionTarget& target() const { return *reinterpret_cast<const FunctionTarget*>(&bits_); }

private:
    alignas(max_align_t) union { char data[target_size]; } bits_;
};

template <size_t inline_callable_size, bool require_inline, typename Result, typename... Args>
class Function;

template <size_t inline_callable_size, bool require_inline, typename Result, typename... Args>
class Function<inline_callable_size, require_inline, Result(Args...)> {
    struct FakeCallable {
        alignas(max_align_t) char bits[mxtl::roundup(inline_callable_size, sizeof(void*))];
    };
    static constexpr size_t inline_target_size =
        sizeof(InlineFunctionTarget<FakeCallable, Result, Args...>);
    static constexpr size_t heap_target_size =
        sizeof(HeapFunctionTarget<FakeCallable, Result, Args...>);
    static constexpr size_t target_size = require_inline ? inline_target_size
                                                         : mxtl::max(inline_target_size, heap_target_size);
    using TargetHolder = FunctionTargetHolder<target_size, Result, Args...>;

public:
    using result_type = Result;

    Function() { holder_.InitializeNullTarget(); }

    Function(decltype(nullptr)) { holder_.InitializeNullTarget(); }

    Function(Function&& other) {
        holder_.MoveInitializeTargetFrom(other.holder_);
        other.holder_.InitializeNullTarget();
    }

    template <typename Callable>
    Function(Callable target) {
        static_assert(!require_inline || sizeof(Callable) <= inline_callable_size,
                      "Callable too large for InlineFunction.");
        holder_.InitializeTarget(mxtl::move(target));
    }

    template <typename Callable>
    Function(Callable target, AllocChecker* ac) {
        static_assert(!require_inline || sizeof(Callable) <= inline_callable_size,
                      "Callable too large for InlineFunction.");
        holder_.InitializeTarget(mxtl::move(target), ac);
    }

    ~Function() {
        holder_.DestroyTarget();
    }

    DISALLOW_COPY_AND_ASSIGN_ALLOW_MOVE(Function);

    explicit operator bool() const {
        return !holder_.target().is_null();
    };

    Result operator()(Args... args) const {
        return holder_.target()(mxtl::forward<Args>(args)...);
    }

    Function& operator=(decltype(nullptr)) {
        holder_.DestroyTarget();
        holder_.InitializeNullTarget();
        return *this;
    }

    Function& operator=(Function&& other) {
        holder_.DestroyTarget();
        holder_.MoveInitializeTargetFrom(other.holder_);
        other.holder_.InitializeNullTarget();
        return *this;
    }

    template <typename Callable>
    Function& operator=(Callable target) {
        SetTarget(mxtl::move(target));
        return *this;
    }

    template <typename Callable>
    void SetTarget(Callable target) {
        static_assert(!require_inline || sizeof(Callable) <= inline_callable_size,
                      "Callable too large for InlineFunction.");
        holder_.DestroyTarget();
        holder_.InitializeTarget(mxtl::move(target));
    }

    template <typename Callable>
    void SetTarget(Callable target, AllocChecker* ac) {
        static_assert(!require_inline || sizeof(Callable) <= inline_callable_size,
                      "Callable too large for InlineFunction.");
        holder_.DestroyTarget();
        holder_.InitializeTarget(mxtl::move(target), ac);
    }

    void swap(Function& other) {
        TargetHolder temp;
        temp.MoveInitializeTargetFrom(holder_);
        holder_.MoveInitializeTargetFrom(other.holder_);
        other.holder_.MoveInitializeTargetFrom(temp);
    }

private:
    TargetHolder holder_;
};

// Helper used by |BindMember| to invoke a pointer to member function.
template <typename R, typename T, typename... Args>
class MemberInvoker final {
public:
    using MemFn = R (T::*)(Args...);

    MemberInvoker(T* instance, MemFn fn)
        : instance_(instance), fn_(fn) {}

    R operator()(Args... args) const {
        return (instance_->*fn_)(mxtl::forward<Args>(args)...);
    }

private:
    T* const instance_;
    MemFn const fn_;
};

} // namespace internal

// The default size allowance for callable objects which can be inlined within
// a function object.  This default allows for inline storage of callables
// consiting of a function pointer and an object pointer (or similar callables
// of the same size).
constexpr size_t kDefaultInlineCallableSize = sizeof(void*) * 2;

// A move-only callable object wrapper.
//
// mxtl::Function<T> behaves like std::function<T> except that it is move-only
// instead of copyable.  This means it can hold mutable lambdas without requiring
// a reference-counted wrapper.
//
// Small callable objects (smaller than |kDefaultInlineCallableSize|) are stored
// inline within the function.  Larger callable objects will be copied to the heap
// if necessary.
//
// See also mxtl::SizedFunction<T, size> and mxtl::InlineFunction<T, size>
// for more control over allocation behavior.
//
// SYNOPSIS:
//
// template <typename Result, typename Args...>
// class Function<Result(Args...)> {
// public:
//     using result_type = Result;
//
//     Function();
//     explicit Function(decltype(nullptr));
//     Function(Function&& other);
//     template <typename Callable>
//     explicit Function(Callable target);
//     template <typename Callable>
//     explicit Function(Callable target, AllocChecker* ac);
//
//     ~Function();
//
//     explicit operator bool() const;
//
//     Result operator()(Args... args) const;
//
//     Function& operator=(decltype(nullptr));
//     Function& operator=(Function&& other);
//     template <typename Callable>
//     Function& operator=(Callable target);
//
//     template <typename Callable>
//     void SetTarget(Callable target);
//     template <typename Callable>
//     void SetTarget(Callable target, AllocChecker* ac);
//
//     void swap(Function& other);
// };
//
// EXAMPLE:
//
// using FoldFunction = mxtl::Function<int(int value, int item)>;
//
// int FoldVector(const mxtl::Vector<int>& in, int value, const FoldFunction& f) {
//     for (auto& item : in) {
//         value = f(value, item);
//     }
//     return value;
// }
//
// int SumItem(int value, int item) {
//     return value + item;
// }
//
// int Sum(const mxtl::Vector<int>& in) {
//     // bind to a function pointer
//     FoldFunction sum(&SumItem);
//     return FoldVector(in, 0, sum);
// }
//
// int AlternatingSum(const mxtl::Vector<int>& in) {
//     // bind to a lambda
//     int sign = 1;
//     FoldFunction alternating_sum([&sign](int value, int item) {
//         value += sign * item;
//         sign *= -1;
//         return value;
//     });
//     return FoldVector(in, 0, alternating_sum);
// }
//
template <typename T>
using Function = mxtl::internal::Function<kDefaultInlineCallableSize, false, T>;

// A move-only callable object wrapper with a explicitly specified (non-default)
// inline callable size preference.
//
// Behaves just like mxtl::Function<T> except that it guarantees that callable
// objects of up to |inline_callable_size| bytes will be stored inline instead
// of on the heap.  This may be useful when you want to optimize storage of
// functions of a known size.
//
// Note that the effective maximum inline callable size may be slightly larger
// due to object alignment and rounding.
template <typename T, size_t inline_callable_size>
using SizedFunction = mxtl::internal::Function<inline_callable_size, false, T>;

// A move-only callable object wrapper which forces callables to be stored inline
// thereby preventing heap allocation.
//
// Behaves just like mxtl::Function<T> except that it will refuse to store a
// callable object larger than |inline_callable_size| (will fail to compile).
template <typename T, size_t inline_callable_size>
using InlineFunction = mxtl::internal::Function<inline_callable_size, true, T>;

// Comparing functions with nullptr.
template <size_t inline_callable_size, bool require_inline, typename Result, typename... Args>
bool operator==(const mxtl::internal::Function<inline_callable_size, require_inline, Result, Args...>& f,
                decltype(nullptr)) {
    return !f;
}
template <size_t inline_callable_size, bool require_inline, typename Result, typename... Args>
bool operator!=(const mxtl::internal::Function<inline_callable_size, require_inline, Result, Args...>& f,
                decltype(nullptr)) {
    return !!f;
}
template <size_t inline_callable_size, bool require_inline, typename Result, typename... Args>
bool operator==(decltype(nullptr),
                const mxtl::internal::Function<inline_callable_size, require_inline, Result, Args...>& f) {
    return !f;
}
template <size_t inline_callable_size, bool require_inline, typename Result, typename... Args>
bool operator!=(decltype(nullptr),
                const mxtl::internal::Function<inline_callable_size, require_inline, Result, Args...>& f) {
    return !!f;
}

// A function which takes no arguments and produces no result.
using Closure = mxtl::Function<void()>;

// Returns a Callable object which invokes a member function of an object.
//
// EXAMPLE:
//
// class Accumulator {
// public:
//     void Add(int value) {
//          sum += value;
//     }
//
//     int sum = 0;
// };
//
// void CountToTen(mxtl::Function<void(int)> function) {
//     for (int i = 1; i <= 10; i++) {
//         function(i);
//     }
// }
//
// int SumToTen() {
//     Accumulator accum;
//     CountToTen(mxtl::BindMember(&accum, &Accumulator::Add));
//     return accum.sum;
// }
template <typename R, typename T, typename... Args>
auto BindMember(T* instance, R (T::*fn)(Args...)) {
    return internal::MemberInvoker<R, T, Args...>(instance, fn);
}

} // namespace mxtl
