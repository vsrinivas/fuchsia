// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <array>
#include <elf.h>
#include <type_traits>
#include <zircon/tls.h>

// The hermetic:: namespace is used for data types meant to be shared between a
// controlling program and its hermetic compute modules.  Data is written by
// the controlling program into VMOs that are mapped into the hermetic process
// where its code can read and/or write them.  When the hermetic process exits,
// the controlling program can read the results written to those VMOs.  Shared
// std::atomic<T> is possible for communicating with hermetic code while it's
// actually running, but that is not so hermetic.

namespace hermetic {

template <typename T>
struct HermeticType {
  static_assert(std::is_void_v<T> || std::is_trivially_copyable_v<T>, "just the bits");
  static_assert(std::is_void_v<T> || std::has_unique_object_representations_v<T>, "no hidden bits");

#if HERMETIC_COMPUTE_MODULE || !defined(__clang_)
  using type = T;
#else
  using type = [[clang::address_space(42), clang::noderef]] T;
#endif
};

template <typename T>
using HermeticPtr = typename HermeticType<T>::type*;

// Use In<T> for data put into the hermetic module's memory for it to read.
// Use Out<T> for data read back from the hermetic module's memory.

#if HERMETIC_COMPUTE_MODULE

template <typename T>
using In = HermeticPtr<const T>;

template <typename T>
using Out = HermeticPtr<T>;

#else  // HERMETIC_COMPUTE_MODULE

template <typename T>
using In = HermeticPtr<T>;

template <typename T>
using Out = HermeticPtr<const T>;

#endif  // HERMETIC_COMPUTE_MODULE

// The Thread Control Block for the initial (usually only) thread.  This is
// set up to match the standard layout per the psABI and <zircon/tls.h>.
//
// The Tcb pointer is passed to the hermetic module's entry point function
// in the second argument register.  engine-start.S sets the machine thread
// pointer as per the psABI based on that.

#ifdef __aarch64__

struct Tcb {
  Tcb(uintptr_t, uintptr_t guard, uintptr_t usp)
      : stack_guard(guard), unsafe_sp(reinterpret_cast<HermeticPtr<std::byte>>(usp)) {}

  uintptr_t stack_guard;
  HermeticPtr<std::byte> unsafe_sp;
  // TPIDR_EL0 points here.

  static constexpr std::ptrdiff_t ThreadPointerOffset() { return sizeof(Tcb); }
};
static_assert(offsetof(Tcb, stack_guard) == sizeof(Tcb) + ZX_TLS_STACK_GUARD_OFFSET);
static_assert(offsetof(Tcb, unsafe_sp) == sizeof(Tcb) + ZX_TLS_UNSAFE_SP_OFFSET);

#elif defined(__x86_64__)

// %fs.base points here, so %fs:0 maps to this struct.
struct Tcb {
  Tcb(uintptr_t tcb, uintptr_t guard, uintptr_t usp)
      : self(reinterpret_cast<HermeticPtr<void>>(tcb)),
        stack_guard(guard),
        unsafe_sp(reinterpret_cast<HermeticPtr<std::byte>>(usp)) {}

  HermeticPtr<void> self;        // Points to this address (%fs:0).
  HermeticPtr<void> reserved{};  // unused (reserved for runtime)
  uintptr_t stack_guard;
  HermeticPtr<std::byte> unsafe_sp;

  static constexpr std::ptrdiff_t ThreadPointerOffset() { return 0; }
};
static_assert(offsetof(Tcb, stack_guard) == ZX_TLS_STACK_GUARD_OFFSET);
static_assert(offsetof(Tcb, unsafe_sp) == ZX_TLS_UNSAFE_SP_OFFSET);

#else
#error "unsupported architecture"
#endif

}  // namespace hermetic
