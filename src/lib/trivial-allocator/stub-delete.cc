// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <new>

#ifndef STUB_BODY
#define STUB_BODY  // Silent no-op
#endif

// See the BUILD.gn target "stub-delete" for more commentary.
//
// The compiler can choose to generate any of the `operator delete` overloads
// for a `delete` statement and any of the `operator delete[]` overloads for a
// `delete[]` statement, depending how much information it kept track of and
// always matching which `operator new` or `operator new[]` overload it would
// have generated for the corresponding `new` expressions.
//
// Any class with a virtual destructor will implicitly define a "delete
// destructor" function in its vtable.  This is reached only via `delete ptr`.
// But even if there is no actual use of `delete` anywhere in the program, the
// vtables will have these functions nonetheless.  They tail-call the `operator
// delete` function to do the actual deletion, so those entry points must be
// defined even if though they can never be reached.

void operator delete(void* ptr) noexcept { STUB_BODY }
void operator delete[](void* ptr) noexcept { STUB_BODY }

void operator delete(void* ptr, std::align_val_t alignment) noexcept { STUB_BODY }
void operator delete[](void* ptr, std::align_val_t alignment) noexcept { STUB_BODY }

void operator delete(void* ptr, size_t size) noexcept { STUB_BODY }
void operator delete[](void* ptr, size_t size) noexcept { STUB_BODY }

void operator delete(void* ptr, size_t size, std::align_val_t al) noexcept { STUB_BODY }
void operator delete[](void* ptr, size_t size, std::align_val_t al) noexcept { STUB_BODY }

// These are the mangled names of all the functions above.  Because these
// functions are magical in the language, the compiler insists on making
// default-visibility definitions regardless of all the ways to tell it to use
// hidden visibility.  So there is nothing left but to go around the compiler's
// back and force them to .hidden via assembler directives.

#if defined(__ELF__)
#define HIDDEN(name) __asm__(".hidden " #name)
#elif defined(__MACH__)
#define HIDDEN(name) __asm__(".private_extern _" #name)
#endif

HIDDEN(_ZdaPv);
HIDDEN(_ZdaPvm);

HIDDEN(_ZdlPv);
HIDDEN(_ZdlPvm);

HIDDEN(_ZdlPvSt11align_val_t);
HIDDEN(_ZdaPvSt11align_val_t);

HIDDEN(_ZdlPvmSt11align_val_t);
HIDDEN(_ZdaPvmSt11align_val_t);
