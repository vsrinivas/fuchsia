// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "stdio-tests.h"

#include "bootfs-tests.h"
#include "tests.h"

namespace {

// The type of FILE* cannot be default-constructed, so we skip the
// TestDefaultConstructedView() test case.

TEST_ITERATION(ZbitlViewStdioTests, StdioTestTraits)

TEST_MUTATION(ZbitlViewStdioTests, StdioTestTraits)

TEST(ZbitlViewStdioTests, CreateFromBogusZbi) {
  ASSERT_NO_FATAL_FAILURE(TestViewFromBogusZbi<StdioTestTraits>());
}

TEST(ZbitlImageStdioTests, ExtendBogusZbi) {
  ASSERT_NO_FATAL_FAILURE(TestExtendBogusZbiImage<StdioTestTraits>());
}

TEST(ZbitlImageStdioTests, Appending) { ASSERT_NO_FATAL_FAILURE(TestAppending<StdioTestTraits>()); }

TEST(ZbitlBootfsStdioTests, Iteration) {
  ASSERT_NO_FATAL_FAILURE(TestBootfsIteration<StdioTestTraits>());
}

TEST(ZbitlBootfsStdioTests, Subdirectory) {
  ASSERT_NO_FATAL_FAILURE(TestBootfsSubdirectory<StdioTestTraits>());
}

}  // namespace
