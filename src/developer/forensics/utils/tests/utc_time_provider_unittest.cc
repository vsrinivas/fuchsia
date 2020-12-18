// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/utils/utc_time_provider.h"

#include <lib/zx/clock.h>
#include <lib/zx/time.h>

#include <memory>
#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/developer/forensics/testing/unit_test_fixture.h"
#include "src/lib/files/file.h"
#include "src/lib/files/path.h"
#include "src/lib/timekeeper/test_clock.h"

namespace forensics {
namespace {

constexpr zx::time_utc kTime((zx::hour(7) + zx::min(14) + zx::sec(52)).get());

class UtcTimeProviderTest : public UnitTestFixture {
 public:
  UtcTimeProviderTest() {
    clock_.Set(kTime);

    zx_clock_create_args_v1_t clock_args{.backstop_time = 0};
    FX_CHECK(zx::clock::create(0u, &clock_args, &clock_handle_) == ZX_OK);

    utc_provider_ = std::make_unique<UtcTimeProvider>(
        dispatcher(), zx::unowned_clock(clock_handle_.get_handle()), &clock_);
  }

 protected:
  void StartClock(const zx::time start_time = zx::time(kTime.get())) {
    if (const zx_status_t status =
            clock_handle_.update(zx::clock::update_args().set_value(start_time));
        status != ZX_OK) {
      FX_PLOGS(FATAL, status) << "Failed to start clock";
    }
  }

 protected:
  timekeeper::TestClock clock_;

 protected:
  zx::clock clock_handle_;
  std::unique_ptr<UtcTimeProvider> utc_provider_;
};

TEST_F(UtcTimeProviderTest, Check_ClockStarts) {
  EXPECT_FALSE(utc_provider_->CurrentTime().has_value());

  StartClock();
  RunLoopUntilIdle();

  ASSERT_TRUE(utc_provider_->CurrentTime().has_value());
  EXPECT_EQ(utc_provider_->CurrentTime().value(), kTime);
}

TEST_F(UtcTimeProviderTest, Check_ClockNeverStarts) {
  for (size_t i = 0; i < 100; ++i) {
    RunLoopFor(zx::hour(23));
    EXPECT_FALSE(utc_provider_->CurrentTime().has_value());
  }
}

TEST_F(UtcTimeProviderTest, Check_CurrentUtcMonotonicDifference) {
  clock_.Set(zx::time(0));
  StartClock(zx::time(0));
  RunLoopUntilIdle();

  zx::time monotonic;
  zx::time_utc utc;

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
      dispatcher(), zx::unowned_clock(clock_handle_.get_handle()), &clock_,
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
  StartClock();
  RunLoopUntilIdle();

  // |is_first_instance| is true becuase the previous UTC-monotonic difference should be read.
  utc_provider_ = std::make_unique<UtcTimeProvider>(
      dispatcher(), zx::unowned_clock(clock_handle_.get_handle()), &clock_,
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
