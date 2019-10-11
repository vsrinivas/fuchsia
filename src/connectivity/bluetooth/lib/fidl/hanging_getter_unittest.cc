// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "hanging_getter.h"

#include <gtest/gtest.h>

namespace bt_lib_fidl {
namespace {

template <typename T, typename Getter = HangingGetter<T>>
class HangingGetterTestBase : public ::testing::Test {
 public:
  bool Watch() {
    return getter_.Watch([this](T value) {
      callback_count_++;
      last_value_ = value;
    });
  }

  int callback_count() const { return callback_count_; }
  const std::optional<T>& last_value() const { return last_value_; }

  Getter* getter() { return &getter_; }

 private:
  int callback_count_ = 0;
  std::optional<T> last_value_;
  Getter getter_;
};

using HangingGetterTest = HangingGetterTestBase<bool>;

TEST_F(HangingGetterTest, Armed) {
  EXPECT_FALSE(getter()->armed());
  EXPECT_TRUE(Watch());
  EXPECT_TRUE(getter()->armed());
}

TEST_F(HangingGetterTest, WatchFailsWhilePending) {
  EXPECT_TRUE(Watch());
  EXPECT_EQ(0, callback_count());
  EXPECT_FALSE(Watch());
  EXPECT_EQ(0, callback_count());
}

TEST_F(HangingGetterTest, WatchCallbackDeferredWithoutAValue) {
  EXPECT_TRUE(Watch());
  EXPECT_EQ(0, callback_count());
  EXPECT_FALSE(last_value().has_value());

  getter()->Set(false);
  EXPECT_EQ(1, callback_count());
  ASSERT_TRUE(last_value().has_value());
  EXPECT_FALSE(*last_value());
}

TEST_F(HangingGetterTest, WatchCallbackRunsRightAwayWithAValue) {
  getter()->Set(false);
  EXPECT_EQ(0, callback_count());
  EXPECT_FALSE(last_value().has_value());

  // Assign the value again to test that the latest value is returned in Watch().
  getter()->Set(true);
  EXPECT_EQ(0, callback_count());
  EXPECT_FALSE(last_value().has_value());
  EXPECT_FALSE(getter()->armed());

  EXPECT_TRUE(Watch());
  EXPECT_EQ(1, callback_count());
  ASSERT_TRUE(last_value().has_value());
  EXPECT_TRUE(*last_value());

  // Calling Watch() again should succeed and defer the callback.
  EXPECT_TRUE(Watch());
  EXPECT_EQ(1, callback_count());
}

TEST_F(HangingGetterTest, WatchClearsExistingValue) {
  getter()->Set(true);
  EXPECT_TRUE(Watch());
  EXPECT_EQ(1, callback_count());
  ASSERT_TRUE(last_value().has_value());
  EXPECT_TRUE(*last_value());

  // Callback should be deferred.
  EXPECT_TRUE(Watch());
  EXPECT_EQ(1, callback_count());

  // Test the deferral.
  getter()->Set(true);
  EXPECT_EQ(2, callback_count());
}

TEST_F(HangingGetterTest, Transform) {
  getter()->Set(false);
  getter()->Transform([](bool current) {
    EXPECT_FALSE(current);
    return true;
  });

  EXPECT_TRUE(Watch());
  EXPECT_EQ(1, callback_count());
  ASSERT_TRUE(last_value().has_value());
  EXPECT_TRUE(*last_value());
}

using HangingVectorGetterTest = HangingGetterTestBase<std::vector<bool>, HangingVectorGetter<bool>>;

TEST_F(HangingVectorGetterTest, AddAndWatch) {
  getter()->Add(false);
  getter()->Add(true);

  EXPECT_TRUE(Watch());
  EXPECT_EQ(1, callback_count());
  EXPECT_TRUE(last_value().has_value());
  EXPECT_EQ(2u, last_value()->size());
  EXPECT_FALSE((*last_value())[0]);
  EXPECT_TRUE((*last_value())[1]);
}

TEST_F(HangingVectorGetterTest, WatchAndAdd) {
  EXPECT_TRUE(Watch());
  EXPECT_EQ(0, callback_count());

  getter()->Add(true);
  EXPECT_EQ(1, callback_count());
  EXPECT_TRUE(last_value().has_value());
  EXPECT_EQ(1u, last_value()->size());
  EXPECT_TRUE((*last_value())[0]);
}

}  // namespace
}  // namespace bt_lib_fidl
