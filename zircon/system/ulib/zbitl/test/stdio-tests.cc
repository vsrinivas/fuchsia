// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "stdio-tests.h"

#include "tests.h"

namespace {

// The type of FILE* cannot be default-constructed, so we skip the
// TestDefaultConstructedView() test case.

TEST(ZbitlViewStdioTests, CrcCheckFailure) {
  ASSERT_NO_FATAL_FAILURES(TestCrcCheckFailure<StdioTestTraits>());
}

TEST_ITERATION(ZbitlViewStdioTests, StdioTestTraits)

TEST_MUTATION(ZbitlViewStdioTests, StdioTestTraits)

}  // namespace
