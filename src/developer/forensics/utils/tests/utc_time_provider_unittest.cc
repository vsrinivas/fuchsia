// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/utils/utc_time_provider.h"

#include <lib/zx/time.h>

#include <memory>
#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/developer/forensics/testing/stubs/utc_provider.h"
#include "src/developer/forensics/testing/unit_test_fixture.h"
#include "src/lib/timekeeper/test_clock.h"

namespace forensics {
namespace {

using stubs::UtcProvider;

constexpr zx::time_utc kTime((zx::hour(7) + zx::min(14) + zx::sec(52)).get());

class UTCTimeProviderTest : public UnitTestFixture {
 public:
  UTCTimeProviderTest() : utc_provider_(std::make_unique<UTCTimeProvider>(services(), &clock_)) {
    clock_.Set(kTime);
  }

 protected:
  void SetUpUtcProviderServer(const std::vector<UtcProvider::Response>& responses) {
    utc_provider_server_ = std::make_unique<stubs::UtcProvider>(dispatcher(), responses);
    InjectServiceProvider(utc_provider_server_.get());
  }

 private:
  timekeeper::TestClock clock_;
  std::unique_ptr<stubs::UtcProviderBase> utc_provider_server_;

 protected:
  std::unique_ptr<UTCTimeProvider> utc_provider_;
};

TEST_F(UTCTimeProviderTest, Check_ReturnsExternal) {
  SetUpUtcProviderServer({
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
  SetUpUtcProviderServer({
      UtcProvider::Response(UtcProvider::Response::Value::kBackstop),
      UtcProvider::Response(UtcProvider::Response::Value::kNoResponse),
  });
  RunLoopUntilIdle();

  EXPECT_FALSE(utc_provider_->CurrentTime().has_value());
}

TEST_F(UTCTimeProviderTest, Check_ServerNeverResponds) {
  SetUpUtcProviderServer({
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
  SetUpUtcProviderServer({
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
}  // namespace forensics
