// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/hermetic-compute/hermetic-engine.h>

struct TestEngine {
    int64_t operator()(float x, double y, long double z) {
        return static_cast<int64_t>(x + y + z);
    }
};

template class HermeticComputeEngine<TestEngine, float, double, long double>;
