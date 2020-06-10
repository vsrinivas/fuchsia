// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_HWSTRESS_COMPILER_H_
#define GARNET_BIN_HWSTRESS_COMPILER_H_

#include <zircon/compiler.h>

namespace hwstress {

// Prevent the compiler from knowing anything about the given value.
//
// For example, "HideFromCompiler(1) + HideFromCompiler(1)" will prevent
// the compiler from constant-folding the resulting value down to 2,
// and instead force it to evaluate the addition.
//
// Will only work with basic types.
template <typename T>
inline T __ALWAYS_INLINE HideFromCompiler(T x) {
  // The following construct tells the compiler to put the variable "x"
  // in a register, and that the (empty) assembly both reads and writes
  // to it.
  //
  // This construct works ARM and x64 architecture, and both GCC and
  // Clang. It's not guarnteed to work everywhere, though, as other
  // architectures or compilers may have their own inline-assembly
  // syntax.
  //
  // An alternative approach that avoids non-standard code would be to
  // write/read a volatile variable; this generates an additional load
  // and store to the stack, however.
  __asm__("" : "+r"(x));
  return x;
}

// Prevent the compiler from assuming anything about the given memory.
//
// For example, a compiler may optimize away a "memset" because it sees
// that the memory is never touched afterwards, or is only written to
// afterwards. The statement "HideMemoryFromCompiler(&memory)" prevents
// the compiler for knowing about that state of memory.
//
template <typename T>
void HideMemoryFromCompiler(T* memory) {
  // Pass the pointer to an empty assembly block as an input, and inform
  // the compiler that memory is read to and possibly modified.
  __asm__ __volatile__("" ::"r"(memory) : "memory");
}

// Force the compiler to evaluate the given value.
//
// For example, the expression "ForceEval(sin(PI))" will force the
// compiler to cacluate sin(PI), and put it in a register. Note that the
// compiler may still perform this evaluation at compile time: see
// "HideFromCompiler" to prevent that.
//
// Will only work with basic types.
template <typename T>
void ForceEval(T x) {
  // Inform the compiler that the (empty) assembly block reads "x" from
  // a register, and the assembly shouldn't be optimized away.
  __asm__ __volatile__("" ::"r"(x));
}

// Unroll the given loop.
//
// For example, the code:
//
//   UNROLL_LOOP for (int i = 0; i < 100; i++) { ... }
//
// will fully unroll the loop. The variants will only unroll the given
// number of times.
#define UNROLL_LOOP _Pragma("unroll")
#define UNROLL_LOOP_2 _Pragma("unroll(2)")
#define UNROLL_LOOP_4 _Pragma("unroll(4)")
#define UNROLL_LOOP_16 _Pragma("unroll(16)")

// Assume the given pointer is aligned to the given alignment.
#define ASSUME_ALIGNED(x) __attribute__((assume_aligned(x)))

}  // namespace hwstress

#endif  // GARNET_BIN_HWSTRESS_COMPILER_H_
