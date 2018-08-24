// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuzz-utils/string-list.h>
#include <unittest/unittest.h>

namespace fuzzing {
namespace testing {
namespace {

bool TestEmpty() {
    BEGIN_TEST;
    StringList list;

    EXPECT_TRUE(list.is_empty());
    EXPECT_NULL(list.first());
    EXPECT_NULL(list.next());

    END_TEST;
}

BEGIN_TEST_CASE(StringListTest)
RUN_TEST(TestEmpty)
END_TEST_CASE(StringListTest)

} // namespace
} // namespace testing
} // namespace fuzzing
