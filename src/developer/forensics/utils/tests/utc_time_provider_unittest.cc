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
#include "src/lib/files/file.h"
#include "src/lib/files/path.h"
#include "src/lib/timekeeper/test_clock.h"

namespace forensics {
namespace {

using stubs::UtcProvider;

constexpr zx::time_utc kTime((zx::hour(7) + zx::min(14) + zx::sec(52)).get());

class UtcTimeProviderTest : public UnitTestFixture {
 public:
  UtcTimeProviderTest() : utc_provider_(std::make_unique<UtcTimeProvider>(services(), &clock_)) {
    clock_.Set(kTime);
  }

 protected:
  void SetUpUtcProviderServer(const std::vector<UtcProvider::Response>& responses) {
    utc_provider_server_ = std::make_unique<stubs::UtcProvider>(dispatcher(), responses);
    InjectServiceProvider(utc_provider_server_.get());
  }

 protected:
  timekeeper::TestClock clock_;
  std::unique_ptr<stubs::UtcProviderBase> utc_provider_server_;

 protected:
  std::unique_ptr<UtcTimeProvider> utc_provider_;
};

TEST_F(UtcTimeProviderTest, Check_ReturnsExternal) {
  SetUpUtcProviderServer({
      UtcProvider::Response(UtcProvider::Response::Value::kExternal),
  });
  RunLoopUntilIdle();

  ASSERT_TRUE(utc_provider_->CurrentTime().has_value());
  EXPECT_EQ(utc_provider_->CurrentTime().value(), kTime);
}

TEST_F(UtcTimeProviderTest, Check_ReturnsBackstop) {
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

TEST_F(UtcTimeProviderTest, Check_ServerNeverResponds) {
  SetUpUtcProviderServer({
      UtcProvider::Response(UtcProvider::Response::Value::kNoResponse),
  });
  RunLoopUntilIdle();

  for (size_t i = 0; i < 100; ++i) {
    RunLoopFor(zx::hour(23));
    EXPECT_FALSE(utc_provider_->CurrentTime().has_value());
  }
}

TEST_F(UtcTimeProviderTest, Check_MultipleCalls) {
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

TEST_F(UtcTimeProviderTest, Check_CurrentUtcMonotonicDifference) {
  SetUpUtcProviderServer({
      UtcProvider::Response(UtcProvider::Response::Value::kExternal),
  });
  RunLoopUntilIdle();

  zx::time monotonic;
  zx::time_utc utc;

  clock_.Set(zx::time(0));

  ASSERT_EQ(clock_.Now(&monotonic), ZX_OK);
  ASSERT_EQ(clock_.Now(&utc), ZX_OK);

  const auto utc_monotonic_difference = utc_provider_->CurrentUtcMonotonicDifference();
  ASSERT_TRUE(utc_monotonic_difference.has_value());
  EXPECT_EQ(monotonic.get() + utc_monotonic_difference.value().get(), utc.get());
}

TEST_F(UtcTimeProviderTest, Check_ReadsPreviousBootUtcMonotonicDifference) {
  ASSERT_TRUE(files::WriteFile("/cache/current_utc_monotonic_difference.txt", "1234"));

  // |is_first_instance| is true becuase the previous UTC-monotonic difference should be read.
  utc_provider_ = std::make_unique<UtcTimeProvider>(
      services(), &clock_,
      PreviousBootFile::FromCache(/*is_first_instance=*/true,
                                  "current_utc_monotonic_difference.txt"));

  const auto previous_utc_monotonic_difference =
      utc_provider_->PreviousBootUtcMonotonicDifference();

  ASSERT_TRUE(previous_utc_monotonic_difference.has_value());
  EXPECT_EQ(previous_utc_monotonic_difference.value().get(), 1234);

  ASSERT_TRUE(files::DeletePath("/cache/curren_utc_monotonic_difference.txt", /*recursive=*/true));
  ASSERT_TRUE(files::DeletePath("/tmp/curren_utc_monotonic_difference.txt", /*recursive=*/true));
}

TEST_F(UtcTimeProviderTest, Check_WritesPreviousBootUtcMonotonicDifference) {
  SetUpUtcProviderServer({
      UtcProvider::Response(UtcProvider::Response::Value::kExternal),
      UtcProvider::Response(UtcProvider::Response::Value::kExternal),
  });
  RunLoopUntilIdle();

  // |is_first_instance| is true becuase the previous UTC-monotonic difference should be read.
  utc_provider_ = std::make_unique<UtcTimeProvider>(
      services(), &clock_,
      PreviousBootFile::FromCache(/*is_first_instance=*/true,
                                  "current_utc_monotonic_difference.txt"));
  RunLoopUntilIdle();

  const auto utc_monotonic_difference = utc_provider_->CurrentUtcMonotonicDifference();
  ASSERT_TRUE(utc_monotonic_difference.has_value());

  std::string content;
  ASSERT_TRUE(files::ReadFileToString("/cache/current_utc_monotonic_difference.txt", &content));

  EXPECT_EQ(content, std::to_string(utc_monotonic_difference.value().get()));

  ASSERT_TRUE(files::DeletePath("/cache/curren_utc_monotonic_difference.txt", /*recursive=*/true));
  ASSERT_TRUE(files::DeletePath("/tmp/curren_utc_monotonic_difference.txt", /*recursive=*/true));
}

}  // namespace
}  // namespace forensics
