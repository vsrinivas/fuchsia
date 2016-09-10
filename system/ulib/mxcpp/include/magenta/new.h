// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stddef.h>

class AllocChecker {
public:
    AllocChecker();
    ~AllocChecker();
    void arm(size_t sz, bool result);
    bool check();
private:
    unsigned state_;
};

void* operator new(size_t);
void* operator new[](size_t);

void* operator new(size_t, AllocChecker* ac);
void* operator new(size_t, void *ptr);

void* operator new[](size_t, AllocChecker* ac);
void* operator new[](size_t, void *ptr);

void operator delete(void *p);
void operator delete[](void *p);
void operator delete(void *p, size_t);
void operator delete[](void *p, size_t);

