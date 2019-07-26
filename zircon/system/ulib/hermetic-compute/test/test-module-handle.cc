// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/hermetic-compute/hermetic-engine.h>

struct TestEngine : public HermeticComputeEngine<TestEngine, int, zx_handle_t, int> {
  int64_t operator()(int x, zx_handle_t h, int y) const {
    // We can't make any system calls that would test whether the handle is
    // truly valid.  So all we can do is test that it appears to be valid.
    if (h == ZX_HANDLE_INVALID) {
      return -1;
    }
    return x + y;
  }
};
