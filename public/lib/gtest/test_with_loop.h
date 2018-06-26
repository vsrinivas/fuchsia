// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_GTEST_TEST_WITH_LOOP_H_
#define LIB_GTEST_TEST_WITH_LOOP_H_

#include "lib/gtest/test_loop_fixture.h"

namespace gtest {

// TODO(joshuaseaton): Remove this once all TestWithLoop instances have been
// renamed to TestLoopFixture.
using TestWithLoop = TestLoopFixture;

}  // namespace gtest

#endif  // LIB_GTEST_TEST_WITH_LOOP_H_
