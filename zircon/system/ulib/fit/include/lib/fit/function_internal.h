// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIT_FUNCTION_INTERNAL_H_
#define LIB_FIT_FUNCTION_INTERNAL_H_

#include <stddef.h>
#include <stdlib.h>

#include <new>
#include <type_traits>
#include <utility>

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
} // namespace fit

#endif // LIB_FIT_FUNCTION_INTERNAL_H_
