// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <magenta/assert.h>
#include <magenta/new.h>
#include <stdlib.h>

enum : unsigned {
    alloc_armed   = 1,
    alloc_ok      = 2,
};

void panic_if_armed(unsigned state) {
    if (state & alloc_armed)
        PANIC("AllocChecker::check() needs to be called\n");
}

AllocChecker::AllocChecker() : state_(0U) {
}

AllocChecker::~AllocChecker() {
    panic_if_armed(state_);
}

void AllocChecker::arm(size_t sz, bool result) {
    panic_if_armed(state_);
    state_ =  alloc_armed |
        ((sz == 0u) ? alloc_ok : (result ? alloc_ok : 0u));
}

bool AllocChecker::check() {
    state_ &= ~alloc_armed;
    return (state_ & alloc_ok) == alloc_ok;
}

void* operator new(size_t s) {
    auto mem = ::malloc(s);
    if (!mem) {
        PANIC("Out of memory (new)\n");
    }
    return mem;
}

void* operator new[](size_t s) {
    auto mem = ::malloc(s);
    if (!mem) {
        PANIC("Out of memory (new[])\n");
    }
    return mem;
}

void* operator new(size_t s, AllocChecker* ac) {
    auto mem = ::malloc(s);
    ac->arm(s, mem != nullptr);
    return mem;
}

void* operator new[](size_t s, AllocChecker* ac) {
    auto mem = ::malloc(s);
    ac->arm(s, mem != nullptr);
    return mem;
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

