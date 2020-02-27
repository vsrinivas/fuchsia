// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gtest/gtest.h"

class HugeTest : public ::testing::TestWithParam<int> {};

TEST_P(HugeTest, Test) { ASSERT_EQ(GetParam(), GetParam()); }

INSTANTIATE_TEST_SUITE_P(HugeStress, HugeTest, ::testing::Range(1, 1001));
