// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cassert>
#include <cstdlib>

void abort() {
    __builtin_trap();
}

void __assert_fail(const char*, const char*, int, const char*) {
    __builtin_trap();
}
