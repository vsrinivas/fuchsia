// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/lib/test/test_fixture.h"

namespace media::audio::test {
//
// TestFixture implementation
//
void TestFixture::SetUp() { ::gtest::RealLoopFixture::SetUp(); }

void TestFixture::TearDown() {
  EXPECT_EQ(error_expected_, error_occurred_);

  ::gtest::RealLoopFixture::TearDown();
}

bool TestFixture::RunUntilComplete(fit::function<bool()> condition) {
  return RunLoopWithTimeoutOrUntil(
      std::move(condition), kDurationResponseExpected, kDurationGranularity);
}

void TestFixture::ExpectCondition(fit::function<bool()> condition) {
  EXPECT_TRUE(RunUntilComplete(std::move(condition)));
}

void TestFixture::ExpectCallback() {
  callback_received_ = false;

  ExpectCondition([this]() { return (error_occurred_ || callback_received_); });

  EXPECT_FALSE(error_occurred_) << kDisconnectErr;
  EXPECT_TRUE(callback_received_);
}

void TestFixture::ExpectError(zx_status_t expected_error) {
  SetNegativeExpectations();
  callback_received_ = false;

  ExpectCondition([this]() { return (error_occurred_ || callback_received_); });

  EXPECT_TRUE(error_occurred_);
  EXPECT_EQ(error_code_, expected_error);
  EXPECT_FALSE(callback_received_) << kCallbackErr;
}

}  // namespace media::audio::test
