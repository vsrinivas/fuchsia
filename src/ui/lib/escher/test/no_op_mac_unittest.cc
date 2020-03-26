// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>

namespace {

// A dummy test to run on macOS instead of the actual Escher unit tests,
// as Escher cannot be built on macs.
TEST(NoOpMacTest, DummyTestCase) {}

}  // namespace
