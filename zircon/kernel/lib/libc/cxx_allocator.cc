// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <new>

// Placement new operators are inlined in the <new> header.
// No linkage definitions are required.
// Vanilla allocating new operators are not allowed in the kernel.
// AllocChecker new operators are defined in <fbl/alloc_checker.h>.

#include <cstdlib>

void operator delete(void* p) { free(p); }

void operator delete[](void* p) { free(p); }

void operator delete(void* p, size_t s) { free(p); }

void operator delete[](void* p, size_t s) { free(p); }

void operator delete(void* p, std::align_val_t align) { free(p); }

void operator delete[](void* p, std::align_val_t align) { free(p); }

void operator delete(void* p, std::size_t s, std::align_val_t align) { free(p); }

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
