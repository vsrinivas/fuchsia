// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/feedback/utils/utc_time_provider.h"

#include <lib/zx/time.h>

#include <memory>
#include <vector>

#include "src/developer/feedback/testing/stubs/utc_provider.h"
#include "src/developer/feedback/testing/unit_test_fixture.h"
#include "src/lib/timekeeper/test_clock.h"
#include "third_party/googletest/googlemock/include/gmock/gmock.h"
#include "third_party/googletest/googletest/include/gtest/gtest.h"

namespace feedback {
namespace {

using stubs::UtcProvider;

constexpr zx::time_utc kTime((zx::hour(7) + zx::min(14) + zx::sec(52)).get());

class UTCTimeProviderTest : public UnitTestFixture {
 public:
  UTCTimeProviderTest() : utc_provider_(std::make_unique<UTCTimeProvider>(services(), clock_)) {
    clock_.Set(kTime);
  }

 protected:
  void SetUpUtcProvider(const std::vector<UtcProvider::Response>& responses) {
    stub_utc_provider_ = std::make_unique<stubs::UtcProvider>(dispatcher(), responses);
    InjectServiceProvider(stub_utc_provider_.get());
  }

 private:
  timekeeper::TestClock clock_;
  std::unique_ptr<stubs::UtcProvider> stub_utc_provider_;

 protected:
  std::unique_ptr<UTCTimeProvider> utc_provider_;
};

TEST_F(UTCTimeProviderTest, Check_ReturnsExternal) {
  SetUpUtcProvider({
      UtcProvider::Response(UtcProvider::Response::Value::kExternal),
  });
  RunLoopUntilIdle();

  ASSERT_TRUE(utc_provider_->CurrentTime().has_value());
  EXPECT_EQ(utc_provider_->CurrentTime().value(), kTime);
}

TEST_F(UTCTimeProviderTest, Check_ReturnsBackstop) {
  // Upon receiving "backstop", |utc_provider_| will make another call to the stub so we need an
  // extra response. We use "no_response" so that |utc_provider_| just waits and doesn't make any
  // more calls.
  SetUpUtcProvider({
      UtcProvider::Response(UtcProvider::Response::Value::kBackstop),
      UtcProvider::Response(UtcProvider::Response::Value::kNoResponse),
  });
  RunLoopUntilIdle();

  EXPECT_FALSE(utc_provider_->CurrentTime().has_value());
}

TEST_F(UTCTimeProviderTest, Check_ServerNeverResponds) {
  SetUpUtcProvider({
      UtcProvider::Response(UtcProvider::Response::Value::kNoResponse),
  });
  RunLoopUntilIdle();

  for (size_t i = 0; i < 100; ++i) {
    RunLoopFor(zx::hour(23));
    EXPECT_FALSE(utc_provider_->CurrentTime().has_value());
  }
}

TEST_F(UTCTimeProviderTest, Check_MultipleCalls) {
  constexpr zx::duration kDelay = zx::msec(5);
  SetUpUtcProvider({
      UtcProvider::Response(UtcProvider::Response::Value::kBackstop, kDelay),
      UtcProvider::Response(UtcProvider::Response::Value::kExternal, kDelay),
  });

  EXPECT_FALSE(utc_provider_->CurrentTime().has_value());

  RunLoopFor(kDelay);
  EXPECT_FALSE(utc_provider_->CurrentTime().has_value());

  RunLoopFor(kDelay);
  ASSERT_TRUE(utc_provider_->CurrentTime().has_value());
  EXPECT_EQ(utc_provider_->CurrentTime().value(), kTime);
}

}  // namespace
}  // namespace feedback
