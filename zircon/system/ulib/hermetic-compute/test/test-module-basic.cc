// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/hermetic-compute/hermetic-engine.h>

struct TestEngine : public HermeticComputeEngine<TestEngine, int, int> {
  int64_t operator()(int x, int y) const { return x + y; }
};
