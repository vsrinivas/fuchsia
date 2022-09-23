// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fd-tests.h"

#include "bootfs-tests.h"
#include "tests.h"

namespace {

TEST(ZbitlViewFdTests, DefaultConstructed) {
  ASSERT_NO_FATAL_FAILURE(TestDefaultConstructedView<FdTestTraits>());
}

TEST(ZbitlViewFdTests, CreateFromBogusZbi) {
  ASSERT_NO_FATAL_FAILURE(TestViewFromBogusZbi<FdTestTraits>());
}

TEST_ITERATION(ZbitlViewFdTests, FdTestTraits)

TEST_MUTATION(ZbitlViewFdTests, FdTestTraits)

TEST(ZbitlImageFdTests, ExtendBogusZbi) {
  ASSERT_NO_FATAL_FAILURE(TestViewFromBogusZbi<FdTestTraits>());
}

TEST(ZbitlImageFdTests, Appending) { ASSERT_NO_FATAL_FAILURE(TestAppending<FdTestTraits>()); }

TEST(ZbitlBootfsFdTests, Iteration) {
  ASSERT_NO_FATAL_FAILURE(TestBootfsIteration<FdTestTraits>());
}

}  // namespace
