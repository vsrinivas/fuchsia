// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <errno.h>
#include <fcntl.h>
#include <fidl/fuchsia.logger/cpp/wire.h>
#include <lib/syslog/global.h>
#include <lib/zx/socket.h>
#include <poll.h>
#include <stdlib.h>
#include <unistd.h>
#include <zircon/errors.h>

#include <climits>

#include <fbl/string_printf.h>
#include <zxtest/zxtest.h>

#include "zircon/system/ulib/syslog/helpers.h"

namespace {

const char* kFileName = syslog::internal::StripPath(__FILE__);
const char* kFilePath = syslog::internal::StripDots(__FILE__);

constexpr std::array<const char*, FX_LOG_MAX_TAGS + 1> kTooManyTags = {"1", "2", "3", "4", "5"};

void smallest_unused_fd(int* fd_out) {
  for (int fd = 0; fd < INT_MAX; ++fd) {
    if (fcntl(fd, F_GETFD, nullptr) < 0) {
      ASSERT_EQ(errno, EBADF, "%s", strerror(errno));
      *fd_out = fd;
      return;
    }
  }
  FAIL("did not find unused FD");
}

// Ensure accessing the global logger is safe when a global object is being torn down.
class LogDuringTeardownTest {
 public:
  ~LogDuringTeardownTest() {
    // This should not crash.
    FX_LOG(INFO, NULL, "message");
  }
} g_log_during_teardown;

TEST(SyslogTests, test_log_init_with_socket) {
  zx::socket socket0, socket1;
  EXPECT_OK(zx::socket::create(0, &socket0, &socket1));
  fx_logger_config_t config = {
      .min_severity = FX_LOG_INFO,
      .log_sink_socket = socket1.release(),
      .tags = nullptr,
      .num_tags = 0,
  };
  EXPECT_OK(fx_log_reconfigure(&config), "");
}

TEST(SyslogTests, test_log_enabled_macro) {
  zx::socket socket0, socket1;
  EXPECT_OK(zx::socket::create(0, &socket0, &socket1));
  fx_logger_config_t config = {
      .min_severity = FX_LOG_INFO,
      .log_sink_socket = socket1.release(),
      .tags = nullptr,
      .num_tags = 0,
  };
  EXPECT_OK(fx_log_reconfigure(&config), "");
  if (FX_VLOG_IS_ENABLED(4)) {
    EXPECT_TRUE(false, "control should not reach this line");
  }
  if (!FX_LOG_IS_ENABLED(INFO)) {
    EXPECT_TRUE(false, "control should not reach this line");
  }
  if (!FX_LOG_IS_ENABLED(ERROR)) {
    EXPECT_TRUE(false, "control should not reach this line");
  }
}

TEST(SyslogTests, test_log_severity_invalid) {
  fx_logger_t* logger = fx_log_get_logger();
  EXPECT_EQ(FX_LOG_INFO, fx_logger_get_min_severity(logger));
  EXPECT_STATUS(ZX_ERR_INVALID_ARGS, fx_logger_set_min_severity(logger, FX_LOG_FATAL + 1));
  EXPECT_EQ(FX_LOG_INFO, fx_logger_get_min_severity(logger));
}

TEST(SyslogTestsEdgeCases, test_global_tag_limit) {
  fx_logger_config_t config = {
      .num_tags = FX_LOG_MAX_TAGS + 1,
  };
  EXPECT_STATUS(ZX_ERR_INVALID_ARGS, fx_log_reconfigure(&config));
}

TEST(SyslogTests, test_log_dont_dup) {
  // Remember the current lowest ununsed fd.
  int fd_before;
  ASSERT_NO_FATAL_FAILURE(smallest_unused_fd(&fd_before));

  // Create a logger
  fx_logger_t* logger;
  zx_status_t status;
  fx_logger_config_t config = {
      .min_severity = FX_LOG_INFO,
      .log_sink_socket = ZX_HANDLE_INVALID,
      .tags = nullptr,
      .num_tags = 0,
  };
  status = fx_logger_create(&config, &logger);
  ASSERT_OK(status);

  // No fd must be taken by the logger.
  int fd_after;
  ASSERT_NO_FATAL_FAILURE(smallest_unused_fd(&fd_after));
  EXPECT_EQ(fd_before, fd_after);

  // Cleanup
  fx_logger_destroy(logger);
}

TEST(SyslogTests, test_fx_logger_create_with_null_config) {
  fx_logger_t* logger;
  EXPECT_STATUS(ZX_ERR_INVALID_ARGS, fx_logger_create(nullptr, &logger));
}

TEST(SyslogTests, test_fx_logger_create_with_null_output_pointer) {
  fx_logger_config_t config = {
      .min_severity = FX_LOG_INFO,
      .log_sink_socket = ZX_HANDLE_INVALID,
      .tags = nullptr,
      .num_tags = 0,
  };
  EXPECT_STATUS(ZX_ERR_INVALID_ARGS, fx_logger_create(&config, nullptr));
}

TEST(SyslogTests, test_fx_logger_reconfigure_with_null_config) {
  // Create a logger
  fx_logger_t* logger;
  fx_logger_config_t config = {
      .min_severity = FX_LOG_INFO,
      .log_sink_socket = ZX_HANDLE_INVALID,
      .tags = nullptr,
      .num_tags = 0,
  };
  ASSERT_OK(fx_logger_create(&config, &logger));

  EXPECT_STATUS(ZX_ERR_INVALID_ARGS, fx_logger_reconfigure(logger, nullptr));

  fx_logger_destroy(logger);
}

TEST(SyslogTests, test_log_sink_channel_closed_on_create_fail) {
  EXPECT_LT(FX_LOG_MAX_TAGS, kTooManyTags.size());

  zx::socket local, remote;
  EXPECT_OK(zx::socket::create(ZX_SOCKET_DATAGRAM, &local, &remote));
  const zx_handle_t passed_handle = remote.release();

  fx_logger_config_t config = {
      .min_severity = FX_LOG_INFO,
      .log_sink_channel = passed_handle,
      .log_sink_socket = ZX_HANDLE_INVALID,
      .tags = kTooManyTags.data(),
      .num_tags = kTooManyTags.size(),
  };

  // This should fail because there are too many tags, and closing the
  // handle should fail because it is already closed.
  fx_logger_t* logger;
  EXPECT_STATUS(ZX_ERR_INVALID_ARGS, fx_logger_create(&config, &logger));
  EXPECT_NE(ZX_HANDLE_INVALID, passed_handle);
  EXPECT_STATUS(ZX_ERR_BAD_HANDLE, zx_handle_close(passed_handle));
}

TEST(SyslogTests, test_log_sink_socket_closed_on_create_fail) {
  EXPECT_LT(FX_LOG_MAX_TAGS, kTooManyTags.size());

  zx::socket local, remote;
  EXPECT_OK(zx::socket::create(ZX_SOCKET_DATAGRAM, &local, &remote));
  const zx_handle_t passed_handle = remote.release();

  fx_logger_config_t config = {
      .min_severity = FX_LOG_INFO,
      .log_sink_channel = ZX_HANDLE_INVALID,
      .log_sink_socket = passed_handle,
      .tags = kTooManyTags.data(),
      .num_tags = kTooManyTags.size(),
  };

  // This should fail because there are too many tags, and closing the
  // handle should fail because it is already closed.
  fx_logger_t* logger;
  EXPECT_STATUS(ZX_ERR_INVALID_ARGS, fx_logger_create(&config, &logger));
  EXPECT_NE(ZX_HANDLE_INVALID, passed_handle);
  EXPECT_STATUS(ZX_ERR_BAD_HANDLE, zx_handle_close(passed_handle));
}

TEST(SyslogTests, test_both_handles_specified_fails_create_and_handles_closed) {
  zx::socket local, remote;
  EXPECT_OK(zx::socket::create(ZX_SOCKET_DATAGRAM, &local, &remote));
  const zx_handle_t passed_log_sink_channel = local.release();
  const zx_handle_t passed_log_sink_socket = remote.release();

  fx_logger_config_t config = {
      .min_severity = FX_LOG_INFO,
      .log_sink_channel = passed_log_sink_channel,
      .log_sink_socket = passed_log_sink_socket,
      .tags = nullptr,
      .num_tags = 0,
  };

  // This should fail because both handles were specified, and closing the
  // handle should fail because it is already closed.
  fx_logger_t* logger;
  EXPECT_STATUS(ZX_ERR_INVALID_ARGS, fx_logger_create(&config, &logger));
  EXPECT_NE(ZX_HANDLE_INVALID, passed_log_sink_channel);
  EXPECT_STATUS(ZX_ERR_BAD_HANDLE, zx_handle_close(passed_log_sink_channel));
  EXPECT_NE(ZX_HANDLE_INVALID, passed_log_sink_socket);
  EXPECT_STATUS(ZX_ERR_BAD_HANDLE, zx_handle_close(passed_log_sink_socket));
}

TEST(SyslogTests, test_log_sink_channel_closed_on_reconfigure_fail) {
  EXPECT_LT(FX_LOG_MAX_TAGS, kTooManyTags.size());

  zx::socket local, remote;
  EXPECT_OK(zx::socket::create(ZX_SOCKET_DATAGRAM, &local, &remote));
  const zx_handle_t passed_handle = remote.release();

  fx_logger_config_t config = {
      .min_severity = FX_LOG_INFO,
      .log_sink_channel = passed_handle,
      .log_sink_socket = ZX_HANDLE_INVALID,
      .tags = kTooManyTags.data(),
      .num_tags = kTooManyTags.size(),
  };

  // This should fail because there are too many tags, and closing the
  // handle should fail because it is already closed.
  EXPECT_STATUS(ZX_ERR_INVALID_ARGS, fx_log_reconfigure(&config));
  EXPECT_NE(ZX_HANDLE_INVALID, passed_handle);
  EXPECT_STATUS(ZX_ERR_BAD_HANDLE, zx_handle_close(passed_handle));
}

TEST(SyslogTests, test_log_sink_socket_closed_on_reconfigure_fail) {
  EXPECT_LT(FX_LOG_MAX_TAGS, kTooManyTags.size());

  zx::socket local, remote;
  EXPECT_OK(zx::socket::create(ZX_SOCKET_DATAGRAM, &local, &remote));
  const zx_handle_t passed_handle = remote.release();

  fx_logger_config_t config = {
      .min_severity = FX_LOG_INFO,
      .log_sink_channel = ZX_HANDLE_INVALID,
      .log_sink_socket = passed_handle,
      .tags = kTooManyTags.data(),
      .num_tags = kTooManyTags.size(),
  };

  // This should fail because there are too many tags, and closing the
  // handle should fail because it is already closed.
  EXPECT_STATUS(ZX_ERR_INVALID_ARGS, fx_log_reconfigure(&config));
  EXPECT_NE(ZX_HANDLE_INVALID, passed_handle);
  EXPECT_STATUS(ZX_ERR_BAD_HANDLE, zx_handle_close(passed_handle));
}

TEST(SyslogTests, test_both_handles_specified_fails_reconfigure_and_handles_closed) {
  zx::socket local, remote;
  EXPECT_OK(zx::socket::create(ZX_SOCKET_DATAGRAM, &local, &remote));
  const zx_handle_t passed_log_sink_channel = local.release();
  const zx_handle_t passed_log_sink_socket = remote.release();

  fx_logger_config_t config = {
      .min_severity = FX_LOG_INFO,
      .log_sink_channel = passed_log_sink_channel,
      .log_sink_socket = passed_log_sink_socket,
      .tags = nullptr,
      .num_tags = 0,
  };

  // This should fail because both handles were specified, and closing the
  // handle should fail because it is already closed.
  EXPECT_STATUS(ZX_ERR_INVALID_ARGS, fx_log_reconfigure(&config));
  EXPECT_NE(ZX_HANDLE_INVALID, passed_log_sink_channel);
  EXPECT_STATUS(ZX_ERR_BAD_HANDLE, zx_handle_close(passed_log_sink_channel));
  EXPECT_NE(ZX_HANDLE_INVALID, passed_log_sink_socket);
  EXPECT_STATUS(ZX_ERR_BAD_HANDLE, zx_handle_close(passed_log_sink_socket));
}

#ifdef SYSLOG_STATIC
TEST(SyslogTests, test_create_with_log_sink_channel_not_supported) {
  zx::socket local, remote;
  EXPECT_OK(zx::socket::create(ZX_SOCKET_DATAGRAM, &local, &remote));
  const zx_handle_t passed_handle = remote.release();

  fx_logger_config_t config = {
      .min_severity = FX_LOG_INFO,
      .log_sink_channel = passed_handle,
      .log_sink_socket = ZX_HANDLE_INVALID,
      .tags = nullptr,
      .num_tags = 0,
  };

  // This should fail because log_sink_channel is not supported, and closing the
  // handle should fail because it is already closed.
  fx_logger_t* logger;
  EXPECT_STATUS(ZX_ERR_INVALID_ARGS, fx_logger_create(&config, &logger));
  EXPECT_NE(ZX_HANDLE_INVALID, passed_handle);
  EXPECT_STATUS(ZX_ERR_BAD_HANDLE, zx_handle_close(passed_handle));
}

TEST(SyslogTests, test_reconfigure_with_log_sink_channel_not_supported) {
  zx::socket local, remote;
  EXPECT_OK(zx::socket::create(ZX_SOCKET_DATAGRAM, &local, &remote));
  const zx_handle_t passed_handle = remote.release();

  fx_logger_config_t config = {
      .min_severity = FX_LOG_INFO,
      .log_sink_channel = passed_handle,
      .log_sink_socket = ZX_HANDLE_INVALID,
      .tags = nullptr,
      .num_tags = 0,
  };

  // This should fail because log_sink_channel is not supported, and closing the
  // handle should fail because it is already closed.
  EXPECT_STATUS(ZX_ERR_INVALID_ARGS, fx_log_reconfigure(&config));
  EXPECT_NE(ZX_HANDLE_INVALID, passed_handle);
  EXPECT_STATUS(ZX_ERR_BAD_HANDLE, zx_handle_close(passed_handle));
}
#endif

}  // namespace
