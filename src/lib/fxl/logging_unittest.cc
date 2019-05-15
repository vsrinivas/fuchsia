// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/lib/fxl/logging.h"

#include <string>

#include "gtest/gtest.h"
#include "src/lib/files/file.h"
#include "src/lib/files/scoped_temp_dir.h"
#include "src/lib/fxl/log_settings.h"

namespace fxl {
namespace {

class LoggingFixture : public ::testing::Test {
 public:
  LoggingFixture()
      : old_settings_(GetLogSettings()), old_stderr_(dup(STDERR_FILENO)) {}
  ~LoggingFixture() {
    SetLogSettings(old_settings_);
    dup2(old_stderr_, STDERR_FILENO);
  }

 private:
  LogSettings old_settings_;
  int old_stderr_;
};

TEST_F(LoggingFixture, Log) {
  LogSettings new_settings;
  EXPECT_EQ(LOG_INFO, new_settings.min_log_level);
  files::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.NewTempFile(&new_settings.log_file));
  SetLogSettings(new_settings);

  FXL_LOG(ERROR) << "something at error";
  FXL_LOG(INFO) << "and some other at info level";

  std::string log;
  ASSERT_TRUE(files::ReadFileToString(new_settings.log_file, &log));

  EXPECT_TRUE(
      log.find(
          "[ERROR:src/lib/fxl/logging_unittest.cc(38)] something at error") !=
      std::string::npos);
  EXPECT_TRUE(
      log.find("[INFO:logging_unittest.cc(39)] and some other at info level") !=
      std::string::npos);
}

#if defined(__Fuchsia__)
TEST_F(LoggingFixture, Plog) {
  LogSettings new_settings;
  EXPECT_EQ(LOG_INFO, new_settings.min_log_level);
  files::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.NewTempFile(&new_settings.log_file));
  SetLogSettings(new_settings);

  FXL_PLOG(ERROR, ZX_OK) << "should be ok";
  FXL_PLOG(ERROR, ZX_ERR_ACCESS_DENIED) << "got access denied";

  std::string log;
  ASSERT_TRUE(files::ReadFileToString(new_settings.log_file, &log));

  EXPECT_TRUE(log.find("should be ok: 0 (ZX_OK)") != std::string::npos);
  EXPECT_TRUE(log.find("got access denied: -30 (ZX_ERR_ACCESS_DENIED)") !=
              std::string::npos);
}
#endif  // defined(__Fuchsia__)

}  // namespace
}  // namespace fxl
