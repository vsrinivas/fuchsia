// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/hermetic-compute/hermetic-engine.h>

#include <cstring>

struct TestEngine : public HermeticComputeEngine<TestEngine, void*, size_t> {
  int64_t operator()(void* data, size_t size) {
    memset(data, 42, size);
    return 0;
  }
};
