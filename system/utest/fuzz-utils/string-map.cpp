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
    map.begin();
    const char** nil = nullptr;
    EXPECT_FALSE(map.next(nil, nil));

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

bool TestBeginAndNext() {
    BEGIN_TEST;
    StringMap map;
    const char* key;
    const char* val;

    map.set("8", "1");
    map.set("7", "2");
    map.set("6", "3");
    map.set("5", "4");
    map.set("4", "5");
    map.set("3", "6");
    map.set("2", "7");
    map.set("1", "8");

    // Iterate over all pairs
    uint8_t keys = 0;
    EXPECT_FALSE(map.next(&key, nullptr));
    map.begin();
    while (map.next(&key, &val)) {
        keys |= static_cast<uint8_t>(1 << (key[0] - '0' - 1));
    }
    EXPECT_EQ(keys, 0xff);

    // Reset and iterate again
    uint8_t vals = 0;
    EXPECT_FALSE(map.next(nullptr, &val));
    map.begin();
    while (map.next(&key, &val)) {
        vals |= static_cast<uint8_t>(1 << (val[0] - '0' - 1));
    }
    EXPECT_EQ(keys, 0xff);

    END_TEST;
}

bool TestEraseAndClear() {
    BEGIN_TEST;
    StringMap map;
    const char* val;

    map.clear();

    map.erase("key1");
    val = map.get("key1");
    EXPECT_NULL(val);

    map.set("key1", "val1");
    map.set("key2", "val2");
    map.erase("key1");

    val = map.get("key1");
    EXPECT_NULL(val);

    val = map.get("key2");
    ASSERT_NONNULL(val);
    EXPECT_STR_EQ(val, "val2");

    map.set("key1", "val1");
    map.clear();

    val = map.get("key1");
    EXPECT_NULL(val);

    val = map.get("key2");
    EXPECT_NULL(val);

    END_TEST;
}

BEGIN_TEST_CASE(StringMapTest)
RUN_TEST(TestEmpty)
RUN_TEST(TestGetAndSet)
RUN_TEST(TestBeginAndNext)
RUN_TEST(TestEraseAndClear)
END_TEST_CASE(StringMapTest)

} // namespace
} // namespace testing
} // namespace fuzzing
