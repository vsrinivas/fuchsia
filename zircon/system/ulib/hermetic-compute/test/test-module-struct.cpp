// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/hermetic-compute/hermetic-engine.h>

#include "test-module-struct.h"

struct TestEngine : public HermeticComputeEngine<TestEngine,
                                                 OneWord,
                                                 MultiWord,
                                                 Tiny,
                                                 Odd> {
    int64_t operator()(const OneWord& a, const MultiWord& b,
                       const Tiny& c, const Odd& d) {
        return a.x + b.x + b.y + b.z + c.x + c.y + d.Total();
    }
};
