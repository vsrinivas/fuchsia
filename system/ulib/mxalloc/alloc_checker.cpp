// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <mxalloc/new.h>

#include <magenta/assert.h>
#include <stdlib.h>

namespace {

enum : unsigned {
    alloc_armed = 1,
    alloc_ok = 2,
};

void panic_if_armed(unsigned state) {
#if LK_DEBUGLEVEL > 1
    if (state & alloc_armed)
        MX_PANIC("AllocChecker::check() needs to be called\n");
#endif
}

} // namespace

AllocChecker::AllocChecker() : state_(0u) {
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
