// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_LIB_SYSCALLS_SAFE_SYSCALL_ARGUMENT_H_
#define ZIRCON_KERNEL_LIB_SYSCALLS_SAFE_SYSCALL_ARGUMENT_H_

#include <stddef.h>
#include <stdint.h>

#include <ktl/bit.h>
#include <ktl/type_traits.h>

// Function arguments of types narrower than a 64-bit register are passed in a
// full 64-bit register or a full 64-bit stack slot.  The machine calling
// conventions either say that the high bits are unspecified, or that the
// narrow integer value is zero-extended or sign-extended as appropriate to its
// type or thereabouts, or doesn't specify it clearly.
//
//  * The aarch64 psABI says high bits are unspecified so the compiler is
//    obliged to ignore them.
//
//  * The x86-64 psABI doesn't clearly specify it (except that bool values are
//    zero-extended from 1 bit to 8 bits); in observed fact, compilers do
//    sometimes assume incoming register values have no excess high bits set.
//
//  * The RISC-V psABI says that values of types narrower than 32 bits are
//    extended as appropriate for their type to 32 bits, and then (they and
//    original 32-bit values) are zero-extended to 64 bits.
//
// Even in a case like aarch64 where the compiler is unambiguously obliged to
// ignore the high bits rather than assume they have been correctly zeroed or
// sign-extended, the kernel should not trust the compiler not to slip up here,
// nor otherwise allow unintended input noise from the user to have any effect
// whatsoever on the kernel (ideally, not even littering the spilled values in
// kernel stack frames differently in case those are accessible via exploits).
//
// So this class handles sanitizing the user argument values into C++ values
// that are safe to trust the compiler with.

// Change this to `Safe = false` manually to exercise the test code without
// using the value-sanitizing code path so the test will fail if the compiler
// doesn't generate defensive code.
template <typename T, bool Safe = true>
struct SafeSyscallArgument;

template <typename T, bool Safe>
struct SafeSyscallArgument {
  // This is the main implementation for handling integer types safely.
  static_assert(Safe);

  // This is the type that generated entry-point wrappers use in the argument
  // declaration.  It's always just a 64-bit integer in a register or stack.
  using RawType = std::conditional_t<std::is_signed_v<T>, int64_t, uint64_t>;

  // This returns a safely valid (if not trustworthy) value of type T by
  // sanitizing the incoming value from the user of any excess high bits.
  // This is a direct user value of type T and not to be considered "safe" as
  // an input value, but it is safely actually of type T by C++ semantics
  // rather than potentially having undefined behavior at the language level.
  static constexpr T Sanitize(RawType value) {
    if constexpr (sizeof(T) == sizeof(RawType)) {
      return ktl::bit_cast<T>(value);
    } else {
      static_assert(sizeof(T) < sizeof(RawType));
      static_assert(ktl::is_integral_v<T>);
      static_assert(ktl::is_signed_v<T> == ktl::is_signed_v<RawType>);
      // This will just truncate the high bits so they are sign- or
      // zero-extended from the narrower integer type in the low bits.
      return static_cast<T>(value);
    }
  }
};

// bool arguments are not currently used, but this would make them safe.
// (They must still be excluded from struct layouts used via copy-in.)
template <>
struct SafeSyscallArgument<bool, true> {
  using RawType = uint64_t;
  static constexpr bool Sanitize(RawType value) { return value & 1; }
};

// This is the unsafe implementation that can be hand-enabled for testing.
// This version approximates the kernel code's potentially-vulnerable state
// before this mitigation was implemented.
template <typename T>
struct SafeSyscallArgument<T, false> {
  using RawType = T;
  static constexpr T Sanitize(RawType value) { return value; }
};

#endif  // ZIRCON_KERNEL_LIB_SYSCALLS_SAFE_SYSCALL_ARGUMENT_H_
