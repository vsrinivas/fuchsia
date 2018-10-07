// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIT_FUNCTION_INTERNAL_H_
#define LIB_FIT_FUNCTION_INTERNAL_H_

#include <stddef.h>
#include <stdlib.h>

#include <memory>
#include <new>
#include <type_traits>
#include <utility>

#include "nullable.h"

namespace fit {
namespace internal {

template <typename Result, typename... Args>
struct target_ops final {
    void* (*get)(void* bits);
    Result (*invoke)(void* bits, Args... args);
    void (*move)(void* from_bits, void* to_bits);
    void (*destroy)(void* bits);
};

template <typename Callable, bool is_inline, typename Result, typename... Args>
struct target;

template <typename Result, typename... Args>
struct target<decltype(nullptr), true, Result, Args...> final {
    static Result invoke(void* bits, Args... args) {
        abort();
    }

    static const target_ops<Result, Args...> ops;
};

inline void* null_target_get(void* bits) {
    return nullptr;
}
inline void null_target_move(void* from_bits, void* to_bits) {}
inline void null_target_destroy(void* bits) {}

template <typename Result, typename... Args>
constexpr target_ops<Result, Args...> target<decltype(nullptr), true, Result, Args...>::ops = {
    &null_target_get,
    &target::invoke,
    &null_target_move,
    &null_target_destroy};

template <typename Callable, typename Result, typename... Args>
struct target<Callable, true, Result, Args...> final {
    static void initialize(void* bits, Callable&& target) {
        new (bits) Callable(std::move(target));
    }
    static Result invoke(void* bits, Args... args) {
        auto& target = *static_cast<Callable*>(bits);
        return target(std::forward<Args>(args)...);
    }
    static void move(void* from_bits, void* to_bits) {
        auto& from_target = *static_cast<Callable*>(from_bits);
        new (to_bits) Callable(std::move(from_target));
        from_target.~Callable();
    }
    static void destroy(void* bits) {
        auto& target = *static_cast<Callable*>(bits);
        target.~Callable();
    }

    static const target_ops<Result, Args...> ops;
};

inline void* inline_target_get(void* bits) {
    return bits;
}

template <typename Callable, typename Result, typename... Args>
constexpr target_ops<Result, Args...> target<Callable, true, Result, Args...>::ops = {
    &inline_target_get,
    &target::invoke,
    &target::move,
    &target::destroy};

template <typename Callable, typename Result, typename... Args>
struct target<Callable, false, Result, Args...> final {
    static void initialize(void* bits, Callable&& target) {
        auto ptr = static_cast<Callable**>(bits);
        *ptr = new Callable(std::move(target));
    }
    static Result invoke(void* bits, Args... args) {
        auto& target = **static_cast<Callable**>(bits);
        return target(std::forward<Args>(args)...);
    }
    static void move(void* from_bits, void* to_bits) {
        auto from_ptr = static_cast<Callable**>(from_bits);
        auto to_ptr = static_cast<Callable**>(to_bits);
        *to_ptr = *from_ptr;
    }
    static void destroy(void* bits) {
        auto ptr = static_cast<Callable**>(bits);
        delete *ptr;
    }

    static const target_ops<Result, Args...> ops;
};

inline void* heap_target_get(void* bits) {
    return *static_cast<void**>(bits);
}

template <typename Callable, typename Result, typename... Args>
constexpr target_ops<Result, Args...> target<Callable, false, Result, Args...>::ops = {
    &heap_target_get,
    &target::invoke,
    &target::move,
    &target::destroy};

} // namespace internal

template <size_t inline_target_size, bool require_inline,
          typename Result, typename... Args>
class function_impl;

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
    using result_type = Result;

    function_impl() {
        initialize_null_target();
    }

    function_impl(decltype(nullptr)) {
        initialize_null_target();
    }

    function_impl(Result (*target)(Args...)) {
        initialize_target(target);
    }

    // For functors, we need to capture the raw type but also restrict on the existence of an
    // appropriate operator () to resolve overloads and implicit casts properly.
    template <typename Callable,
              typename = decltype(std::declval<Callable>()(std::declval<Args>()...))>
    function_impl(Callable target) {
        initialize_target(std::move(target));
    }

    function_impl(const function_impl& other) = delete;

    function_impl(function_impl&& other) {
        move_target_from(std::move(other));
    }

    ~function_impl() {
        destroy_target();
    }

    explicit operator bool() const {
        return ops_ != &null_target_type::ops;
    };

    Result operator()(Args... args) const {
        return ops_->invoke(&bits_, std::forward<Args>(args)...);
    }

    function_impl& operator=(decltype(nullptr)) {
        destroy_target();
        initialize_null_target();
        return *this;
    }

    template <typename Callable>
    function_impl& operator=(Callable target) {
        destroy_target();
        initialize_target(std::move(target));
        return *this;
    }

    function_impl& operator=(const function_impl& other) = delete;

    function_impl& operator=(function_impl&& other) {
        if (&other == this)
            return *this;
        destroy_target();
        move_target_from(std::move(other));
        return *this;
    }

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

    template <typename Callable>
    Callable* target() {
        check_target_type<Callable>();
        return static_cast<Callable*>(ops_->get(&bits_));
    }

    template <typename Callable>
    const Callable* target() const {
        check_target_type<Callable>();
        return static_cast<Callable*>(ops_->get(&bits_));
    }

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
        if (::fit::is_null(target)) {
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
};

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

} // namespace fit

#endif // LIB_FIT_FUNCTION_INTERNAL_H_
