// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cstring>
#include <lib/hermetic-compute/hermetic-engine.h>

struct TestEngine : public HermeticComputeEngine<TestEngine> {
  int64_t operator()() const {
    volatile std::byte a[16 << 10];
    for (auto &x : a) {
      x = std::byte{42};
    }
    return 0;
  }
};
