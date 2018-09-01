// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuzz-utils/string-map.h>
#include <unittest/unittest.h>

namespace fuzzing {
namespace testing {
namespace {

bool TestEmpty() {
    BEGIN_TEST;
    StringMap map;

    EXPECT_TRUE(map.is_empty());
    EXPECT_EQ(0, map.size());

    END_TEST;
}

BEGIN_TEST_CASE(StringMapTest)
RUN_TEST(TestEmpty)
END_TEST_CASE(StringMapTest)

} // namespace
} // namespace testing
} // namespace fuzzing
