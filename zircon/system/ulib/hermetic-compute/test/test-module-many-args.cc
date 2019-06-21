// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/hermetic-compute/hermetic-engine.h>

struct TestEngine : public HermeticComputeEngine<
    TestEngine, int, int, int, int, int, int, int, int, int, int, int, int> {
    int64_t operator()(int a1, int a2, int a3, int a4, int a5,
                       int a6, int a7, int a8, int a9, int a10,
                       int a11, int a12) const {
        return a1 + a2 + a3 + a4 + a5 + a6 + a7 + a8 + a9 + a10 + a11 + a12;
    }
};
