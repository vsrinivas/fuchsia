// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <zircon/assert.h>

#include <new>

// Any class with a virtual destructor will implicitly define a "delete
// destructor" function in its vtable.  This is reached only via `delete ptr`.
// There is no use of `delete` in phys code, but the vtables will have these
// functions nonetheless.  They tail-call the `operator delete` function to do
// the actual deletion, so those entry points must be defined even though they
// can never be reached.

namespace {

void DummyDelete() { ZX_PANIC("should never be reached"); }

}  // namespace

void operator delete(void* p) { DummyDelete(); }

void operator delete[](void* p) { DummyDelete(); }

void operator delete(void* p, size_t s) { DummyDelete(); }

void operator delete[](void* p, size_t s) { DummyDelete(); }

void operator delete(void* p, std::align_val_t align) { DummyDelete(); }

void operator delete[](void* p, std::align_val_t align) { DummyDelete(); }

void operator delete(void* p, std::size_t s, std::align_val_t align) { DummyDelete(); }

void operator delete[](void* p, std::size_t s, std::align_val_t align) { DummyDelete(); }

// These are the mangled names of all the functions above.  Because these
// functions are magical in the language, the compiler insists on making
// default-visibility definitions regardless of all the ways to tell it to use
// hidden visibility.  So there is nothing left but to go around the compiler's
// back and force them to .hidden via assembler directives.
asm(".hidden _ZdaPv");
asm(".hidden _ZdaPvm");
asm(".hidden _ZdlPv");
asm(".hidden _ZdlPvm");
asm(".hidden _ZdlPvSt11align_val_t");
asm(".hidden _ZdaPvSt11align_val_t");
asm(".hidden _ZdlPvmSt11align_val_t");
asm(".hidden _ZdaPvmSt11align_val_t");
