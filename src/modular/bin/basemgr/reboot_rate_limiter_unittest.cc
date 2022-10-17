// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/modular/bin/basemgr/reboot_rate_limiter.h"

#include <zircon/assert.h>

#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>

#include <gtest/gtest.h>

#include "src/lib/files/file.h"
#include "src/lib/files/scoped_temp_dir.h"
#include "src/lib/fxl/strings/string_number_conversions.h"

namespace modular {

class RebootRateLimiterTest : public testing::Test {
 protected:
  static constexpr char kRebootTrackerFile[] = "reboot_tracker";
  static constexpr char kTimestampFormat[] = "%F %T";

  using TimePoint = RebootRateLimiter::TimePoint;
  using SystemClock = RebootRateLimiter::SystemClock;

  std::string GetTmpFilePath() { return tmp_dir.path() + "/" + kRebootTrackerFile; }

  static std::string GenerateTestFileContent(size_t minutes = 0, size_t counter = 1) {
    std::stringstream ss;
    ss << "2000-01-01 00:" << std::setfill('0') << std::setw(2) << minutes << ":00" << std::endl
       << counter;
    return ss.str();
  }

  static TimePoint GenerateTestTimePoint(size_t minutes = 0) {
    std::tm test_timestamp{.tm_sec = 0,
                           .tm_min = static_cast<int>(minutes),
                           .tm_hour = 0,
                           .tm_mday = 1,
                           .tm_mon = 0,
                           .tm_year = 2000 - 1900,
                           .tm_wday = 0,
                           .tm_yday = 0,
                           .tm_isdst = 0};
    return SystemClock::from_time_t(timegm(&test_timestamp));
  }

  std::string GetTmpFileContent() {
    std::string actual_content;
    ZX_ASSERT_MSG(files::ReadFileToString(GetTmpFilePath(), &actual_content),
                  "Failed to read file %s: %s", GetTmpFilePath().data(), strerror(errno));

    return actual_content;
  }

 private:
  files::ScopedTempDir tmp_dir = files::ScopedTempDir();
};

TEST_F(RebootRateLimiterTest, CanRebootReturnsTrueIfFileDoesntExist) {
  auto rate_limiter = RebootRateLimiter(GetTmpFilePath());
  zx::result<bool> can_reboot_or = rate_limiter.CanReboot();

  ASSERT_TRUE(can_reboot_or.is_ok());
  EXPECT_TRUE(can_reboot_or.value());
}

TEST_F(RebootRateLimiterTest, CanRebootReturnsTrueIfAfterBackoffThreshold) {
  ASSERT_TRUE(files::WriteFile(GetTmpFilePath(), GenerateTestFileContent()));

  auto rate_limiter = RebootRateLimiter(GetTmpFilePath());
  zx::result<bool> can_reboot_or = rate_limiter.CanReboot(GenerateTestTimePoint(/*minutes=*/3));

  ASSERT_TRUE(can_reboot_or.is_ok());
  EXPECT_TRUE(can_reboot_or.value());
}

TEST_F(RebootRateLimiterTest, CanRebootReturnsFalseIfBeforeBackoffThreshold) {
  auto timepoint = GenerateTestTimePoint();
  ASSERT_TRUE(files::WriteFile(GetTmpFilePath(), GenerateTestFileContent()));

  auto rate_limiter = RebootRateLimiter(GetTmpFilePath(), /*backoff_base=*/5);
  zx::result<bool> can_reboot_or = rate_limiter.CanReboot(timepoint);

  ASSERT_TRUE(can_reboot_or.is_ok());
  EXPECT_FALSE(can_reboot_or.value());
}

TEST_F(RebootRateLimiterTest, CanRebootReturnsTrueIfBeyondMaxDelay) {
  auto timepoint = GenerateTestTimePoint();
  ASSERT_TRUE(
      files::WriteFile(GetTmpFilePath(), GenerateTestFileContent(/*minutes=0*/ 0, /*counter=*/4)));

  auto rate_limiter = RebootRateLimiter(GetTmpFilePath(), /*backoff_base=*/2, /*max_delay=*/16);
  zx::result<bool> can_reboot_or = rate_limiter.CanReboot(timepoint);

  ASSERT_TRUE(can_reboot_or.is_ok());
  EXPECT_TRUE(can_reboot_or.value());
}

TEST_F(RebootRateLimiterTest, CanRebootFlushesFileAfterTTLExpiresAndReturnsTrue) {
  ASSERT_TRUE(files::WriteFile(GetTmpFilePath(), GenerateTestFileContent()));

  auto rate_limiter = RebootRateLimiter(GetTmpFilePath(), /*backoff_base=*/10, /*max_delay=*/100,
                                        /*tracking_file_ttl=*/std::chrono::minutes(1));
  zx::result<bool> can_reboot_or = rate_limiter.CanReboot(GenerateTestTimePoint(/*minutes=*/2));

  EXPECT_FALSE(files::IsFile(GetTmpFilePath()));

  ASSERT_TRUE(can_reboot_or.is_ok());
  EXPECT_TRUE(can_reboot_or.value());
}

TEST_F(RebootRateLimiterTest, UpdateTrackingFileCreatesFileIfNonExistent) {
  auto rate_limiter = RebootRateLimiter(GetTmpFilePath());
  auto timepoint = GenerateTestTimePoint();
  ASSERT_TRUE(rate_limiter.UpdateTrackingFile(timepoint).is_ok());

  EXPECT_EQ(GetTmpFileContent(), GenerateTestFileContent());
}

TEST_F(RebootRateLimiterTest, UpdateTrackingFileUpdatesCounter) {
  ASSERT_TRUE(files::WriteFile(GetTmpFilePath(), GenerateTestFileContent()));

  auto rate_limiter = RebootRateLimiter(GetTmpFilePath());

  auto timepoint = GenerateTestTimePoint(/*minutes=*/5);
  ASSERT_TRUE(rate_limiter.UpdateTrackingFile(timepoint).is_ok());

  std::string expected_file_content = GenerateTestFileContent(/*minutes=*/5, /*counter=*/2);
  EXPECT_EQ(GetTmpFileContent(), expected_file_content);
}

}  // namespace modular
