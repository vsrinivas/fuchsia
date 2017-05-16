// Copyright 2017 The Fuchsia Authors. All rights reserved.
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

void* operator new(size_t, AllocChecker* ac);
void* operator new[](size_t, AllocChecker* ac);
