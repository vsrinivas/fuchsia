// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/lib/fxl/logging.h"

#include <string>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "src/lib/files/file.h"
#include "src/lib/files/scoped_temp_dir.h"
#include "src/lib/fxl/log_settings.h"

#ifdef __Fuchsia__
#include <lib/syslog/global.h>
#include <lib/syslog/logger.h>
#include <lib/syslog/wire_format.h>
#include <lib/zx/socket.h>
#endif

namespace fxl {
namespace {

#ifdef __Fuchsia__

struct LogPacket {
  fx_log_metadata_t metadata;
  std::vector<std::string> tags;
  std::string message;
};

LogPacket ReadPacket(const zx::socket& local) {
  LogPacket result;
  fx_log_packet_t packet;
  local.read(0, &packet, sizeof(packet), nullptr);
  result.metadata = packet.metadata;
  int pos = 0;
  while (packet.data[pos]) {
    int tag_len = packet.data[pos++];
    result.tags.emplace_back(packet.data + pos, tag_len);
    pos += tag_len;
  }
  result.message.append(packet.data + pos + 1);
  return result;
}

#endif

class LoggingFixture : public ::testing::Test {
 public:
  LoggingFixture() : old_settings_(GetLogSettings()), old_stderr_(dup(STDERR_FILENO)) {}
  ~LoggingFixture() {
    SetLogSettings(old_settings_);
#ifdef __Fuchsia__
    // Go back to using STDERR.
    fx_logger_t* logger = fx_log_get_logger();
    fx_logger_activate_fallback(logger, -1);
#else
    dup2(old_stderr_, STDERR_FILENO);
#endif
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

  int error_line = __LINE__ + 1;
  FXL_LOG(ERROR) << "something at error";

  int info_line = __LINE__ + 1;
  FXL_LOG(INFO) << "and some other at info level";

  std::string log;
  ASSERT_TRUE(files::ReadFileToString(new_settings.log_file, &log));

#ifdef __Fuchsia__
  EXPECT_THAT(log, testing::HasSubstr("ERROR: [src/lib/fxl/logging_unittest.cc(" +
                                      std::to_string(error_line) + ")] something at error"));

  EXPECT_THAT(log, testing::HasSubstr("INFO: [logging_unittest.cc(" + std::to_string(info_line) +
                                      ")] and some other at info level"));
#else
  EXPECT_THAT(log, testing::HasSubstr("[ERROR:src/lib/fxl/logging_unittest.cc(" +
                                      std::to_string(error_line) + ")] something at error"));

  EXPECT_THAT(log, testing::HasSubstr("[INFO:logging_unittest.cc(" + std::to_string(info_line) +
                                      ")] and some other at info level"));

#endif
}

TEST_F(LoggingFixture, DVLogNoMinLevel) {
  LogSettings new_settings;
  EXPECT_EQ(LOG_INFO, new_settings.min_log_level);
  files::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.NewTempFile(&new_settings.log_file));
  SetLogSettings(new_settings);

  FXL_DVLOG(1) << "hello";

  std::string log;
  ASSERT_TRUE(files::ReadFileToString(new_settings.log_file, &log));

  EXPECT_EQ(log, "");
}

TEST_F(LoggingFixture, DVLogWithMinLevel) {
  LogSettings new_settings;
  EXPECT_EQ(LOG_INFO, new_settings.min_log_level);
  new_settings.min_log_level = -1;
  files::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.NewTempFile(&new_settings.log_file));
  SetLogSettings(new_settings);

  FXL_DVLOG(1) << "hello";

  std::string log;
  ASSERT_TRUE(files::ReadFileToString(new_settings.log_file, &log));

#if defined(NDEBUG)
  EXPECT_EQ(log, "");
#else
  EXPECT_THAT(log, testing::HasSubstr("hello"));
#endif
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

  EXPECT_THAT(log, testing::HasSubstr("should be ok: 0 (ZX_OK)"));
  EXPECT_THAT(log, testing::HasSubstr("got access denied: -30 (ZX_ERR_ACCESS_DENIED)"));
}

TEST_F(LoggingFixture, UseSyslog) {
  // Initialize syslog with a socket.
  zx::socket local, remote;
  EXPECT_EQ(ZX_OK, zx::socket::create(ZX_SOCKET_DATAGRAM, &local, &remote));
  const char* tags[] = {"tags1", "tag2"};
  fx_logger_config_t config = {.min_severity = FX_LOG_INFO,
                               .console_fd = -1,
                               .log_service_channel = remote.release(),
                               .tags = tags,
                               .num_tags = 2};
  ASSERT_EQ(ZX_OK, fx_log_reconfigure(&config));

  // Write a message using FXL_LOG and verify that it's forwarded to syslog.
  std::string msg = "test message";
  FXL_LOG(ERROR) << msg;
  LogPacket packet = ReadPacket(local);
  EXPECT_EQ(2u, packet.tags.size());
  EXPECT_EQ(tags[0], packet.tags[0]);
  EXPECT_EQ(tags[1], packet.tags[1]);

  // |msg| should appear at the end of the log.
  EXPECT_EQ(packet.message.rfind(msg), packet.message.size() - msg.size());

  // The error message should not contain the severity since it's already
  // included in the metadata.
  EXPECT_EQ(packet.message.find("ERROR"), std::string::npos);

  // Cleanup. Make sure syslog switches back to using stderr.
  fx_logger_activate_fallback(fx_log_get_logger(), -1);
}

#endif  // defined(__Fuchsia__)

}  // namespace
}  // namespace fxl
