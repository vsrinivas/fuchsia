// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <mxcpp/new.h>

#include <magenta/assert.h>
#include <stdlib.h>

// The kernel does not want non-AllocCheckered non-placement new
// overloads, but userspace can have them.
#if !_KERNEL
void* operator new(size_t s) {
    auto mem = ::malloc(s);
    if (!mem) {
        MX_PANIC("Out of memory (new)\n");
    }
    return mem;
}

void* operator new[](size_t s) {
    auto mem = ::malloc(s);
    if (!mem) {
        MX_PANIC("Out of memory (new[])\n");
    }
    return mem;
}
#endif // !_KERNEL

void* operator new(size_t s, const std::nothrow_t&) noexcept {
    return ::malloc(s);
}

void* operator new[](size_t s, const std::nothrow_t&) noexcept {
    return ::malloc(s);
}

void* operator new(size_t , void *p) {
    return p;
}

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
