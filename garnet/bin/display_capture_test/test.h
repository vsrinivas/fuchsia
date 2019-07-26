// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_DISPLAY_CAPTURE_TEST_TEST_H_
#define GARNET_BIN_DISPLAY_CAPTURE_TEST_TEST_H_

#include <vector>
#include "context.h"

#define DISPLAY_TEST(name, test_fn) static bool name = display_test::RegisterTest(#name, test_fn)

namespace display_test {

using Test = fit::function<void(Context* context)>;

extern bool RegisterTest(const char* name, Test fn);

}  // namespace display_test

#endif  // GARNET_BIN_DISPLAY_CAPTURE_TEST_TEST_H_
