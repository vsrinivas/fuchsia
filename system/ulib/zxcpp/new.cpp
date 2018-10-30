// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <new>

// Placement new operators are inlined in the <new> header.
// No linkage definitions are required.

#include <zircon/assert.h>
#include <stdlib.h>

// In ASan builds, the ASan runtime supplies the operator new/delete functions.
// Those versions check for mismatches between allocation entry path and
// deallocation entry path, so we don't want to override them.  Also, in
// certain complex static linking situations, it's difficult to avoid sometimes
// getting the definition of one from this library and another from libc++.
#if !__has_feature(address_sanitizer)

# if !_KERNEL

// The kernel does not want non-AllocCheckered non-placement new
// overloads, but userspace can have them.
void* operator new(size_t s) {
    if (s == 0u) {
        s = 1u;
    }
    auto mem = ::malloc(s);
    if (!mem) {
        ZX_PANIC("Out of memory (new)\n");
    }
    return mem;
}

void* operator new[](size_t s) {
    if (s == 0u) {
        s = 1u;
    }
    auto mem = ::malloc(s);
    if (!mem) {
        ZX_PANIC("Out of memory (new[])\n");
    }
    return mem;
}

void* operator new(size_t s, const std::nothrow_t&) noexcept {
    if (s == 0u) {
        s = 1u;
    }
    return ::malloc(s);
}

void* operator new[](size_t s, const std::nothrow_t&) noexcept {
    if (s == 0u) {
        s = 1u;
    }
    return ::malloc(s);
}

# else  // _KERNEL

// kernel versions may pass through the call site to the underlying allocator
void* operator new(size_t s, void* caller, const std::nothrow_t&) noexcept {
    if (s == 0u) {
        s = 1u;
    }
    return ::malloc_debug_caller(s, caller);
}

void* operator new[](size_t s, void* caller, const std::nothrow_t&) noexcept {
    if (s == 0u) {
        s = 1u;
    }
    return ::malloc_debug_caller(s, caller);
}

# endif  // _KERNEL

void operator delete(void *p) {
    return ::free(p);
}

void operator delete[](void *p) {
    return ::free(p);
}

void operator delete(void *p, size_t s) {
    return ::free(p);
}

void operator delete[](void *p, size_t s) {
    return ::free(p);
}

#endif  // !__has_feature(address_sanitizer)

// These are the mangled names of all the functions above.  Because these
// functions are magical in the language, the compiler insists on making
// default-visibility definitions regardless of all the ways to tell it to use
// hidden visibility.  So there is nothing left but to go around the compiler's
// back and force them to .hidden via assembler directives.  These declarations
// have no effect and do no harm when not all of these functions are defined
// here (kernel, ASan).
asm(".hidden _ZdaPv");
asm(".hidden _ZdaPvm");
asm(".hidden _ZdlPv");
asm(".hidden _ZdlPvm");
asm(".hidden _Znam");
asm(".hidden _ZnamPv");
asm(".hidden _ZnamRKSt9nothrow_t");
asm(".hidden _Znwm");
asm(".hidden _ZnwmPv");
asm(".hidden _ZnwmRKSt9nothrow_t");
