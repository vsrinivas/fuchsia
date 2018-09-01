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

bool TestGetAndSet() {
    BEGIN_TEST;
    StringMap map;
    const char* val;

    val = map.get("key1");
    EXPECT_NULL(val);

    map.set("key1", "val1");
    map.set("key2", "val2");

    val = map.get("key1");
    ASSERT_NONNULL(val);
    EXPECT_STR_EQ(val, "val1");

    val = map.get("key2");
    ASSERT_NONNULL(val);
    EXPECT_STR_EQ(val, "val2");

    map.set("key1", "val2");

    val = map.get("key1");
    ASSERT_NONNULL(val);
    EXPECT_STR_EQ(val, "val2");

    val = map.get("key2");
    ASSERT_NONNULL(val);
    EXPECT_STR_EQ(val, "val2");

    END_TEST;
}

BEGIN_TEST_CASE(StringMapTest)
RUN_TEST(TestEmpty)
RUN_TEST(TestGetAndSet)
END_TEST_CASE(StringMapTest)

} // namespace
} // namespace testing
} // namespace fuzzing
