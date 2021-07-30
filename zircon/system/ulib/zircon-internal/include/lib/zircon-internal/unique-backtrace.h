// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_ZIRCON_INTERNAL_UNIQUE_BACKTRACE_H_
#define LIB_ZIRCON_INTERNAL_UNIQUE_BACKTRACE_H_

// Calling this ensures that neither the compiler (including link-time
// optimization, or LTO) nor the linker (including aggressive identical code
// folding, or ICF) can combine that code path with another code path from a
// different source location.  This ensures that a backtrace through a function
// calling this will always clearly indicate the actual name and source
// location of that function.
//
// Nothing prevents the compiler from moving or eliminating this code path in
// normal ways.  So placing this in dead code (e.g. after a return) will not
// prevent the containing function from being conflated with others.  Usually
// it's best to make this the first thing in the function.  Note that in C++, a
// method in an inner class, or a lambda, is its own function lexically nested
// inside a containing function.  The inner method or lambda is itself subject
// to being combined with unrelated code unless this is called inside each
// particular function independent of the function that contains it lexically.
//
// Implementation notes:
//
//  * The C and C++ rules around object identity and pointer values require
//    that each appearance of a separate `static const ...` variable definition
//    in the source refer to a unique runtime address even if they all have the
//    same constant value.  (This is distinct from e.g. a string literal where
//    it's unspecified whether two occurrences of "foo" will have distinct
//    addresses at runtime or not.  Two named entities always do.)  Note that
//    two expansions of the same macro constitute two separate appearances in
//    the source code just as if the macro expansion were typed out in the
//    source, but an inline function definition counts as only one appearance
//    no matter how many times it's inlined, and still counts as only one if
//    it's in a header file that's reused across many translation units.
//
//  * This `__asm__` construct requires the compiler to materialize that unique
//    object's address in a register at runtime (due to the "r" constraint).
//    Nothing uses that register and the compiler will immediately reuse it for
//    other purposes.  But using `__asm__` means the compiler doesn't know that
//    and so never believes it can optimize out that code.  (Even with LTO,
//    even with the integrated assembler, and even with literally empty
//    assembly text, the compiler obeys the direction to produce the operand
//    for that assembly text to use.)  There's no other overhead at all, not
//    even a function call.  Materializing a `static const` data address into a
//    register is cheaper than a function call in the direct number of
//    instructions and CPU cycles involved but even more significantly in the
//    secondary effects on code generation in the calling function of the added
//    register pressure implied by the ABI calling convention.
//
//  * Making this a macro rather than an inline function is the simplest way to
//    be sure it can't be combined with other instances.  The all-caps flat
//    macro namespace can also be the same for C code as opposed to finding a
//    C++ namespace for the inline function to reside in.
//
#define ENSURE_UNIQUE_BACKTRACE()                                    \
  ({                                                                 \
    static const char ensure_unique_backtrace_instance = 0;          \
    __asm__ volatile("" : : "r"(&ensure_unique_backtrace_instance)); \
  })

// This causes an immediate crash that's guaranteed to have a unique backtrace.
// See notes on ENSURE_UNIQUE_BACKTRACE(), above.
#define CRASH_WITH_UNIQUE_BACKTRACE() (ENSURE_UNIQUE_BACKTRACE(), __builtin_trap())

#endif  // LIB_ZIRCON_INTERNAL_UNIQUE_BACKTRACE_H_
