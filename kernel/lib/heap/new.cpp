// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2006-2015 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <new.h>
#include <debug.h>
#include <lib/heap.h>

enum : unsigned {
    alloc_armed   = 1,
    alloc_ok      = 2,
};

void panic_if_armed(unsigned state) {
#if LK_DEBUGLEVEL > 1
    if (state & alloc_armed)
        panic("AllocChecker::check() needs to be called\n");
#endif
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

void *operator new(size_t s, AllocChecker* ac) {
    auto mem = malloc(s);
    ac->arm(s, mem != nullptr);
    return mem;
}

void *operator new[](size_t s, AllocChecker* ac) {
    auto mem = malloc(s);
    ac->arm(s, mem != nullptr);
    return mem;
}

void *operator new(size_t , void *p) {
    return p;
}

void operator delete(void *p) {
    return free(p);
}

void operator delete[](void *p) {
    return free(p);
}

