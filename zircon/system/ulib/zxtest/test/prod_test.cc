// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zxtest/cpp/zxtest_prod.h>
#include <zxtest/zxtest.h>

// Tests ZXTEST_FRIEND_TEST.
class FriendChecker {
 private:
  ZXTEST_FRIEND_TEST(ProdTest, Friend);
  int value = 7;
};

// This test is really a compilation test that ensures the ZXTEST_FRIEND_TEST statement in the
// FriendChecker class takes effect.
TEST(ProdTest, Friend) {
  FriendChecker checker;
  checker.value++;
  EXPECT_TRUE(checker.value);
}
