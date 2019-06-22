// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

// Include our sister file from the same directory we're in, first.
#include "hermetic-data.h"

#include <array>
#include <cassert>
#include <cstdarg>
#include <cstdint>
#include <elf.h>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <zircon/syscalls.h>

#if __cplusplus > 201703L
#include <span>
#endif

// Forward declaration.
template <typename T>
class HermeticImportAgent;

// This is the primitive base class of hermetic compute engines.  Most engines
// use HermeticComputeEngine instead, which requires a leading vDSO argument.
//
// Engine is the callable derived class being defined.  It will be
// default-constructed and then immediately called as void(Args...).
// Then the process will crash, so it's not expected to return.
//
template <typename Engine, typename... Args>
class HermeticComputeEngineBase {
public:
    using type = void(Args...);

private:
    // Just instantiating the template defines this function, which always has
    // the extern "C" linkage name called from engine-start.S.  This function
    // is responsible for unwrapping the arguments (however many uintptr_t
    // arguments the caller passed), creating and calling the Engine object.
    [[noreturn, gnu::used]] static void EngineMain(uintptr_t,
                                                   ...) __asm__("_start");

    // Function calls don't guarantee the order of evaluation, so just putting
    // va_arg(args, uintptr_t)... into a call isn't kosher.  However, list
    // initialization does guarantees the order of evaluation.  Hopefully the
    // compiler will actually optimize out the copy to the array.
    template <size_t... I>
    static std::array<uintptr_t, 1 + sizeof...(I)> ArgArray(
        uintptr_t first,
        // The va_list is unused when sizeof...(I) == 0.
        [[maybe_unused]] va_list args,
        std::index_sequence<I...>) {
        // The expression has to use I somehow to make ... expansion work.
        return {first, ((void)I, va_arg(args, uintptr_t))...};
    }
};

// This is the common base class of hermetic compute engines.  The controlling
// process much pass a leading HermeticComputeProcess::Vdso argument before the
// arguments corresponding to Args...
//
// Engine is the callable derived class being defined.  It will be
// default-constructed and then immediately called as int64_t(Args...).
// Then the process will exit with the returned exit status code.
//
template <typename Engine, typename... Args>
struct HermeticComputeEngine :
    public HermeticComputeEngineBase<HermeticComputeEngine<Engine, Args...>,
                                     hermetic::In<Elf64_Ehdr>,
                                     Args...> {
    using type = int64_t(Args...);

    void operator()(hermetic::In<Elf64_Ehdr> vdso, Args&&... args) {
        const auto process_exit = reinterpret_cast<decltype(zx_process_exit)*>(
            reinterpret_cast<uintptr_t>(vdso) + vdso->e_entry);
        // The engine's constructor runs just before the call and its
        // destructor runs just after (before exit).
        int64_t result = Engine()(std::forward<Args>(args)...);
        process_exit(result);
    }
};

template <typename Engine, typename... Args>
void HermeticComputeEngineBase<Engine, Args...>::EngineMain(uintptr_t first,
                                                            ...) {
    static_assert(std::is_invocable_r_v<void, Engine, Args...>);

#ifndef __clang__
    // GCC doesn't respect the __asm__("...") linkage name in a template
    // instantiation.  These shenanigans amount to defining an alias with
    // the proper name, but don't require deducing the mangled symbol name.
# ifdef __x86_64__
#  define HermeticComputeEngine_tailcall_asm "jmp %c0"
#  define HermeticComputeEngine_tailcall_constraint "i"
# elif defined(__aarch64__)
#  define HermeticComputeEngine_tailcall_asm "b %0"
#  define HermeticComputeEngine_tailcall_constraint "S"
# else
#  error "what architecture?"
# endif
    __asm__(".pushsection .text._start.trampoline,\"ax\",%%progbits\n"
            ".globl _start\n"
            ".hidden _start\n"
            "_start:\n"
            ".cfi_startproc\n"
            HermeticComputeEngine_tailcall_asm "\n"
            ".cfi_endproc\n"
            ".size _start,.-_start\n"
            ".popsection" ::
            HermeticComputeEngine_tailcall_constraint(&EngineMain));
# undef HermeticComputeEngine_tailcall_asm
# undef HermeticComputeEngine_tailcall_constraint
#endif

    // Each argument to Engine is responsible for some number of uintptr_t
    // arguments to this function.  All the arguments together are packed
    // the same way as a tuple of those argument types.
    using Agent = HermeticImportAgent<std::tuple<Args...>>;

    if (Agent::kArgumentCount == 0) {
        // The engine's constructor runs just before the call and its
        // destructor runs just after.
        std::apply(Engine(), Agent()({}));
    } else {
        using Indices = std::make_index_sequence<Agent::kArgumentCount - 1>;
        va_list args;
        va_start(args, first);
        std::apply(Engine(), Agent()(ArgArray(first, args, Indices())));
        va_end(args);
    }

    // Crash if the engine returned.
    __builtin_trap();
}

// This can be specialized to provide transparent argument-passing support for
// nontrivial types.  The default implementation handles trivial types that
// don't have padding bits.
//
// See HermeticExportAgent (hermetic-compute.h).  The HermeticImportAgent
// for a type provides `static constexpr size_t kArgumentCount` and is
// callable with std::array<uintptr_t, kArgumentCount>.
//
// HermeticImportAgent is default constructed and then immediately called
// with the uintptr_t values to unpack.  It returns the imported argument.
//
template <typename T>
class HermeticImportAgent {
public:
    using type = T;

    static_assert(std::is_standard_layout_v<T>,
                  "need converter for non-standard-layout type");

    static_assert(std::has_unique_object_representations_v<T> ||
                  std::is_floating_point_v<T>,
                  "need converter for type with padding bits");

    static constexpr auto kArgumentCount =
        (sizeof(T) + sizeof(uintptr_t) - 1) / sizeof(uintptr_t);

    auto operator()(const std::array<uintptr_t, kArgumentCount>& words) {
        if constexpr (std::is_integral_v<T> && kArgumentCount == 1) {
            // Small integer types were coerced to uintptr_t.
            return static_cast<T>(words[0]);
        } else {
            // Other things were turned into arrays of uintptr_t.
            T result;
            static_assert(sizeof(result) <= sizeof(uintptr_t[kArgumentCount]));
            memcpy(&result, words.data(), sizeof(result));
            return result;
        }
    }
};

// Specialization for tuples.
template <typename... T>
class HermeticImportAgent<std::tuple<T...>> {
public:
    using type = std::tuple<T...>;

    static constexpr auto kArgumentCount =
        (HermeticImportAgent<T>::kArgumentCount + ... + 0);

    using Words = std::array<uintptr_t, kArgumentCount>;

    type operator()(const Words& words) {
        // Each Unpack call peels off its words and advances the index.
        // At the end the index matches kArgumentCount.  Note that this
        // is list-initialization and therefore the evaluation order is
        // guaranteed to be left to right (unlike e.g. function calls).
        size_t i = 0;
        return {Unpack<T>(words, &i)...};
    }

private:
    // Split off one element's worth of uintptr_t words into a smaller array.
    template <size_t... I>
    std::array<uintptr_t, sizeof...(I)> ElementSlice(
        const Words& words, size_t first, std::index_sequence<I...>) {
        return {words[first + I]...};
    }

    // Unpack one element, advancing the next argument index past what it used.
    template <typename Element>
    auto Unpack(const Words& words, size_t* next) {
        using Agent = HermeticImportAgent<Element>;
        constexpr auto nargs = Agent::kArgumentCount;
        using Indices = std::make_index_sequence<nargs>;
        size_t i = *next;
        *next += nargs;
        return Agent()(ElementSlice(words, i, Indices()));
    }
};

// Specialization for pairs.
template <typename T1, typename T2>
struct HermeticImportAgent<std::pair<T1, T2>> {
    using type = std::pair<T1, T2>;
    using Tuple = std::tuple<T1, T2>;

    static auto constexpr kArgumentCount =
        HermeticImportAgent<Tuple>::kArgumentCount;

    auto operator()(const std::array<uintptr_t, kArgumentCount>& words) {
        // Detuplize.
        return std::make_from_tuple<type>(HermeticImportAgent<Tuple>()(words));
    }
};

// Specialization for std::array.
template <typename T, size_t N>
class HermeticImportAgent<std::array<T, N>> {
public:
    using type = std::array<T, N>;

    using ElementAgent = HermeticImportAgent<T>;
    static constexpr auto kElementSpan = ElementAgent::kArgumentCount;

    static constexpr auto kArgumentCount = kElementSpan * N;

    using Words = std::array<uintptr_t, kArgumentCount>;

    type operator()(const Words& words) {
        return Slice(words, std::make_index_sequence<N>());
    }

private:
    using ElementWords = std::array<uintptr_t, kElementSpan>;

    // Split off one element's worth of uintptr_t words into a smaller array.
    template <size_t... I>
    ElementWords ElementSlice(const Words& words, size_t i,
                              std::index_sequence<I...>) {
        return {words[i + I]...};
    }

    // Make an array by passing each slice through the element type's agent.
    template <size_t... I>
    type Slice(const Words& words, std::index_sequence<I...>) {
        return {ElementAgent()(
                ElementSlice(words, I * kElementSpan,
                             std::make_index_sequence<kElementSpan>()))...};
    }
};

// Specialization for std::basic_string_view (aka std::string_view),
// imported as pointer and byte size (regardless of element type).
//
// This packing protocol is used by e.g. VmoSpan.  Note that there is no
// export agent for std::string_view and the like.
template <typename T>
class HermeticImportAgent<std::basic_string_view<T>> {
public:
    using type = std::basic_string_view<T>;

    static constexpr size_t kArgumentCount = 2;

    type operator()(const std::array<uintptr_t, 2> args) {
        assert(args[0] % alignof(T) == 0);
        assert(args[1] % sizeof(T) == 0);
        return type(reinterpret_cast<const T*>(args[0]), args[1] / sizeof(T));
    }
};

#if __cplusplus > 201703L
// Specialization for std::span, imported as pointer and byte size
// (regardless of element type).
template <typename T>
class HermeticImportAgent<std::span<T>> {
public:
    using type = std::span<T>;

    static constexpr size_t kArgumentCount = 2;

    type operator()(const std::array<uintptr_t, 2> args) {
        assert(args[0] % alignof(T) == 0);
        assert(args[1] % sizeof(T) == 0);
        return type(reinterpret_cast<T*>(args[0]), args[1] / sizeof(T));
    }
};
#endif
