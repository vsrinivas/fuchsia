// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <errno.h>
#include <fcntl.h>
#include <fuchsia/logger/llcpp/fidl.h>
#include <lib/syslog/global.h>
#include <lib/zx/socket.h>
#include <poll.h>
#include <stdlib.h>
#include <unistd.h>

#include <fbl/string_printf.h>
#include <zxtest/zxtest.h>

#include "zircon/system/ulib/syslog/helpers.h"

namespace {

const char* kFileName = syslog::internal::StripPath(__FILE__);
const char* kFilePath = syslog::internal::StripDots(__FILE__);

constexpr std::array<const char*, FX_LOG_MAX_TAGS + 1> kTooManyTags = {"1", "2", "3", "4", "5"};

bool ends_with(const char* str, const fbl::String& suffix) {
  size_t str_len = strlen(str);
  size_t suffix_len = suffix.size();
  if (str_len < suffix_len) {
    return false;
  }
  str += str_len - suffix_len;
  return strcmp(str, suffix.c_str()) == 0;
}

static void smallest_unused_fd(int* fd) {
  char name[] = "/tmp/syslog_test.XXXXXX";
  *fd = mkstemp(name);
  ASSERT_GT(*fd, -1);
  close(*fd);
  int status = remove(name);
  ASSERT_EQ(0, status);
}

}  // namespace

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
  EXPECT_EQ(ZX_OK, zx::socket::create(0, &socket0, &socket1));
  fx_logger_config_t config = {.min_severity = FX_LOG_INFO,
                               .console_fd = -1,
                               .log_sink_socket = socket1.release(),
                               .tags = nullptr,
                               .num_tags = 0};
  EXPECT_EQ(ZX_OK, fx_log_reconfigure(&config), "");
}

TEST(SyslogTests, test_log_enabled_macro) {
  zx::socket socket0, socket1;
  EXPECT_EQ(ZX_OK, zx::socket::create(0, &socket0, &socket1));
  fx_logger_config_t config = {.min_severity = FX_LOG_INFO,
                               .console_fd = -1,
                               .log_sink_socket = socket1.release(),
                               .tags = nullptr,
                               .num_tags = 0};
  EXPECT_EQ(ZX_OK, fx_log_reconfigure(&config), "");
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

static inline zx_status_t init_helper(int fd, const char** tags, size_t ntags) {
  fx_logger_config_t config = {.min_severity = FX_LOG_INFO,
                               .console_fd = fd,
                               .log_sink_socket = ZX_HANDLE_INVALID,
                               .tags = tags,
                               .num_tags = ntags};

  return fx_log_reconfigure(&config);
}

TEST(SyslogTests, test_log_simple_write) {
  int pipefd[2];
  EXPECT_NE(pipe2(pipefd, O_NONBLOCK), -1, "");
  EXPECT_EQ(ZX_OK, init_helper(pipefd[0], NULL, 0), "");
  FX_LOG(INFO, NULL, "test message");
  char buf[256];
  size_t n = read(pipefd[1], buf, sizeof(buf));
  EXPECT_GT(n, 0u, "");
  buf[n] = 0;
  EXPECT_TRUE(ends_with(buf, "test message\n"), "%s", buf);
  close(pipefd[1]);
}

TEST(SyslogTests, test_log_write) {
  int pipefd[2];
  EXPECT_NE(pipe2(pipefd, O_NONBLOCK), -1, "");
  EXPECT_EQ(ZX_OK, init_helper(pipefd[0], NULL, 0), "");
  int line = __LINE__ + 1;
  FX_LOGF(INFO, NULL, "%d, %s", 10, "just some number");
  char buf[256];
  size_t n = read(pipefd[1], buf, sizeof(buf));
  EXPECT_GT(n, 0u, "");
  buf[n] = 0;
  EXPECT_TRUE(
      ends_with(buf, fbl::StringPrintf("INFO: [%s(%d)] 10, just some number\n", kFileName, line)),
      "%s", buf);
  close(pipefd[1]);
}

TEST(SyslogTests, test_log_preprocessed_message) {
  int pipefd[2];
  EXPECT_NE(pipe2(pipefd, O_NONBLOCK), -1, "");
  EXPECT_EQ(ZX_OK, init_helper(pipefd[0], NULL, 0), "");
  int line = __LINE__ + 1;
  FX_LOG(INFO, NULL, "%d, %s");
  char buf[256];
  size_t n = read(pipefd[1], buf, sizeof(buf));
  EXPECT_GT(n, 0u, "");
  buf[n] = 0;
  EXPECT_TRUE(ends_with(buf, fbl::StringPrintf("INFO: [%s(%d)] %%d, %%s\n", kFileName, line)), "%s",
              buf);
  close(pipefd[1]);
}

TEST(SyslogTests, test_log_severity) {
  struct pollfd fd;
  int pipefd[2];
  EXPECT_NE(pipe2(pipefd, O_NONBLOCK), -1, "");
  EXPECT_EQ(ZX_OK, init_helper(pipefd[0], NULL, 0), "");
  FX_LOG_SET_SEVERITY(WARNING);
  FX_LOGF(INFO, NULL, "%d, %s", 10, "just some number");
  fd.fd = pipefd[1];
  fd.events = POLLIN;
  EXPECT_EQ(poll(&fd, 1, 1), 0, "");
  close(pipefd[1]);
}

TEST(SyslogTests, test_log_severity_invalid) {
  int pipefd[2];
  EXPECT_NE(pipe2(pipefd, O_NONBLOCK), -1, "");
  EXPECT_EQ(ZX_OK, init_helper(pipefd[0], NULL, 0), "");
  fx_logger_t* logger = fx_log_get_logger();
  EXPECT_EQ(FX_LOG_INFO, fx_logger_get_min_severity(logger));
  EXPECT_EQ(ZX_ERR_INVALID_ARGS, fx_logger_set_min_severity(logger, FX_LOG_FATAL + 1));
  EXPECT_EQ(FX_LOG_INFO, fx_logger_get_min_severity(logger));
}

TEST(SyslogTests, test_log_write_with_tag) {
  int pipefd[2];
  EXPECT_NE(pipe2(pipefd, O_NONBLOCK), -1, "");
  EXPECT_EQ(ZX_OK, init_helper(pipefd[0], NULL, 0), "");
  int line = __LINE__ + 1;
  FX_LOGF(INFO, "tag", "%d, %s", 10, "just some string");
  char buf[256];
  size_t n = read(pipefd[1], buf, sizeof(buf));
  EXPECT_GT(n, 0u, "");
  buf[n] = 0;
  EXPECT_TRUE(ends_with(buf, fbl::StringPrintf("[tag] INFO: [%s(%d)] 10, just some string\n",
                                               kFileName, line)),
              "%s", buf);
  close(pipefd[1]);
}

TEST(SyslogTests, test_log_write_with_global_tag) {
  int pipefd[2];
  EXPECT_NE(pipe2(pipefd, O_NONBLOCK), -1, "");
  const char* tags[] = {"gtag"};
  EXPECT_EQ(ZX_OK, init_helper(pipefd[0], tags, 1), "");
  int line = __LINE__ + 1;
  FX_LOGF(INFO, "tag", "%d, %s", 10, "just some string");
  char buf[256];
  size_t n = read(pipefd[1], buf, sizeof(buf));
  EXPECT_GT(n, 0u, "");
  buf[n] = 0;
  EXPECT_TRUE(ends_with(buf, fbl::StringPrintf("[gtag, tag] INFO: [%s(%d)] 10, just some string\n",
                                               kFileName, line)),
              "%s", buf);
  close(pipefd[1]);
}

TEST(SyslogTests, test_log_write_with_multi_global_tag) {
  int pipefd[2];
  EXPECT_NE(pipe2(pipefd, O_NONBLOCK), -1, "");
  const char* tags[] = {"gtag", "gtag2"};
  EXPECT_EQ(ZX_OK, init_helper(pipefd[0], tags, 2), "");
  int line = __LINE__ + 1;
  FX_LOGF(INFO, "tag", "%d, %s", 10, "just some string");
  char buf[256];
  size_t n = read(pipefd[1], buf, sizeof(buf));
  EXPECT_GT(n, 0u, "");
  buf[n] = 0;
  EXPECT_TRUE(
      ends_with(buf, fbl::StringPrintf("[gtag, gtag2, tag] INFO: [%s(%d)] 10, just some string\n",
                                       kFileName, line)),
      "%s", buf);
  close(pipefd[1]);
}

TEST(SyslogTestsEdgeCases, test_global_tag_limit) {
  EXPECT_NE(ZX_OK, init_helper(-1, NULL, FX_LOG_MAX_TAGS + 1), "");
}

TEST(SyslogTestsEdgeCases, test_msg_length_limit) {
  constexpr size_t kMessageSize = llcpp::fuchsia::logger::MAX_DATAGRAM_LEN_BYTES + 5;
  int pipefd[2];
  EXPECT_NE(pipe2(pipefd, O_NONBLOCK), -1, "");
  EXPECT_EQ(ZX_OK, init_helper(pipefd[0], NULL, 0), "");
  char msg[kMessageSize] = {0};
  char buf[kMessageSize] = {0};
  memset(msg, 'a', sizeof(msg) - 1);
  FX_LOGF(INFO, NULL, "%s", msg);
  size_t n = read(pipefd[1], buf, sizeof(buf));
  EXPECT_GT(n, 0u, "");
  msg[n] = 0;
  EXPECT_TRUE(ends_with(buf, "a...\n"), "%s", buf);

  msg[0] = '%';
  msg[1] = 's';
  FX_LOG(INFO, NULL, msg);
  n = read(pipefd[1], buf, sizeof(buf));
  EXPECT_GT(n, 0u, "");
  msg[n] = 0;
  EXPECT_TRUE(ends_with(buf, "a...\n"), "%s", buf);
  close(pipefd[1]);
}

TEST(SyslogTests, test_vlog_simple_write) {
  int pipefd[2];
  EXPECT_NE(pipe2(pipefd, O_NONBLOCK), -1, "");
  EXPECT_EQ(ZX_OK, init_helper(pipefd[0], NULL, 0), "");
  FX_LOG_SET_VERBOSITY(5);  // INFO - 5
  int line = __LINE__ + 1;
  FX_VLOG(5, NULL, "test message");
  char buf[256];
  size_t n = read(pipefd[1], buf, sizeof(buf));
  EXPECT_GT(n, 0u, "");
  buf[n] = 0;
  EXPECT_TRUE(
      ends_with(buf, fbl::StringPrintf("VLOG(5): [%s(%d)] test message\n", kFileName, line)), "%s",
      buf);
  close(pipefd[1]);
}

TEST(SyslogTests, test_vlog_write) {
  int pipefd[2];
  EXPECT_NE(pipe2(pipefd, O_NONBLOCK), -1, "");
  EXPECT_EQ(ZX_OK, init_helper(pipefd[0], NULL, 0), "");
  FX_LOG_SET_VERBOSITY(1);  // INFO - 1
  int line = __LINE__ + 1;
  FX_VLOGF(1, NULL, "%d, %s", 10, "just some number");
  char buf[256];
  size_t n = read(pipefd[1], buf, sizeof(buf));
  EXPECT_GT(n, 0u, "");
  buf[n] = 0;
  EXPECT_TRUE(ends_with(buf, fbl::StringPrintf("VLOG(1): [%s(%d)] 10, just some number\n",
                                               kFileName, line)),
              "%s", buf);
  close(pipefd[1]);
}

TEST(SyslogTests, test_log_reconfiguration) {
  // Initialize with no tags.
  int pipefd[2];
  EXPECT_NE(pipe2(pipefd, O_NONBLOCK), -1, "");
  EXPECT_EQ(ZX_OK, init_helper(pipefd[0], NULL, 0), "");
  int line = __LINE__ + 1;
  FX_LOG(INFO, NULL, "Hi");
  char buf[256];
  size_t n = read(pipefd[1], buf, sizeof(buf));
  EXPECT_GT(n, 0u, "");
  buf[n] = 0;
  EXPECT_TRUE(ends_with(buf, fbl::StringPrintf("INFO: [%s(%d)] Hi\n", kFileName, line)), "%s", buf);

  // Now reconfigure the logger and add tags.
  const char* tags[] = {"tag1", "tag2"};
  EXPECT_EQ(ZX_OK, init_helper(-1, tags, 2), "");
  line = __LINE__ + 1;
  FX_LOG(INFO, NULL, "Hi");
  n = read(pipefd[1], buf, sizeof(buf));
  EXPECT_GT(n, 0u, "");
  buf[n] = 0;
  EXPECT_TRUE(
      ends_with(buf, fbl::StringPrintf("[tag1, tag2] INFO: [%s(%d)] Hi\n", kFileName, line)), "%s",
      buf);

  close(pipefd[1]);
}

TEST(SyslogTests, test_log_dont_dup) {
  // Remember the current lowest ununsed fd.
  int fd_before;
  ASSERT_NO_FATAL_FAILURES(smallest_unused_fd(&fd_before));

  // Create a logger
  fx_logger_t* logger;
  zx_status_t status;
  fx_logger_config_t config = {.min_severity = FX_LOG_INFO,
                               .console_fd = -1,
                               .log_sink_socket = ZX_HANDLE_INVALID,
                               .tags = nullptr,
                               .num_tags = 0};
  status = fx_logger_create(&config, &logger);
  ASSERT_EQ(ZX_OK, status);

  // No fd must be taken by the logger.
  int fd_after;
  ASSERT_NO_FATAL_FAILURES(smallest_unused_fd(&fd_after));
  EXPECT_EQ(fd_before, fd_after);

  // Cleanup
  fx_logger_destroy(logger);
}

TEST(SyslogTests, test_fx_logger_create_with_null_config) {
  fx_logger_t* logger;
  EXPECT_EQ(ZX_ERR_INVALID_ARGS, fx_logger_create(nullptr, &logger));
}

TEST(SyslogTests, test_fx_logger_create_with_null_output_pointer) {
  fx_logger_config_t config = {.min_severity = FX_LOG_INFO,
                               .console_fd = -1,
                               .log_sink_socket = ZX_HANDLE_INVALID,
                               .tags = nullptr,
                               .num_tags = 0};
  EXPECT_EQ(ZX_ERR_INVALID_ARGS, fx_logger_create(&config, nullptr));
}

TEST(SyslogTests, test_fx_logger_reconfigure_with_null_config) {
  // Create a logger
  fx_logger_t* logger;
  fx_logger_config_t config = {.min_severity = FX_LOG_INFO,
                               .console_fd = -1,
                               .log_sink_socket = ZX_HANDLE_INVALID,
                               .tags = nullptr,
                               .num_tags = 0};
  ASSERT_EQ(ZX_OK, fx_logger_create(&config, &logger));

  EXPECT_EQ(ZX_ERR_INVALID_ARGS, fx_logger_reconfigure(logger, nullptr));

  fx_logger_destroy(logger);
}

TEST(SyslogTests, test_log_sink_channel_closed_on_create_fail) {
  EXPECT_LT(FX_LOG_MAX_TAGS, kTooManyTags.size());

  zx::socket local, remote;
  EXPECT_EQ(ZX_OK, zx::socket::create(ZX_SOCKET_DATAGRAM, &local, &remote));
  const zx_handle_t passed_handle = remote.release();

  fx_logger_config_t config = {.min_severity = FX_LOG_INFO,
                               .console_fd = -1,
                               .log_sink_channel = passed_handle,
                               .log_sink_socket = ZX_HANDLE_INVALID,
                               .tags = kTooManyTags.data(),
                               .num_tags = kTooManyTags.size()};

  // This should fail because there are too many tags, and closing the
  // handle should fail because it is already closed.
  fx_logger_t* logger;
  EXPECT_EQ(ZX_ERR_INVALID_ARGS, fx_logger_create(&config, &logger));
  EXPECT_NE(ZX_HANDLE_INVALID, passed_handle);
  EXPECT_EQ(ZX_ERR_BAD_HANDLE, zx_handle_close(passed_handle));
}

TEST(SyslogTests, test_log_sink_socket_closed_on_create_fail) {
  EXPECT_LT(FX_LOG_MAX_TAGS, kTooManyTags.size());

  zx::socket local, remote;
  EXPECT_EQ(ZX_OK, zx::socket::create(ZX_SOCKET_DATAGRAM, &local, &remote));
  const zx_handle_t passed_handle = remote.release();

  fx_logger_config_t config = {.min_severity = FX_LOG_INFO,
                               .console_fd = -1,
                               .log_sink_channel = ZX_HANDLE_INVALID,
                               .log_sink_socket = passed_handle,
                               .tags = kTooManyTags.data(),
                               .num_tags = kTooManyTags.size()};

  // This should fail because there are too many tags, and closing the
  // handle should fail because it is already closed.
  fx_logger_t* logger;
  EXPECT_EQ(ZX_ERR_INVALID_ARGS, fx_logger_create(&config, &logger));
  EXPECT_NE(ZX_HANDLE_INVALID, passed_handle);
  EXPECT_EQ(ZX_ERR_BAD_HANDLE, zx_handle_close(passed_handle));
}

TEST(SyslogTests, test_both_handles_specified_fails_create_and_handles_closed) {
  zx::socket local, remote;
  EXPECT_EQ(ZX_OK, zx::socket::create(ZX_SOCKET_DATAGRAM, &local, &remote));
  const zx_handle_t passed_log_sink_channel = local.release();
  const zx_handle_t passed_log_sink_socket = remote.release();

  fx_logger_config_t config = {.min_severity = FX_LOG_INFO,
                               .console_fd = -1,
                               .log_sink_channel = passed_log_sink_channel,
                               .log_sink_socket = passed_log_sink_socket,
                               .tags = nullptr,
                               .num_tags = 0};

  // This should fail because both handles were specified, and closing the
  // handle should fail because it is already closed.
  fx_logger_t* logger;
  EXPECT_EQ(ZX_ERR_INVALID_ARGS, fx_logger_create(&config, &logger));
  EXPECT_NE(ZX_HANDLE_INVALID, passed_log_sink_channel);
  EXPECT_EQ(ZX_ERR_BAD_HANDLE, zx_handle_close(passed_log_sink_channel));
  EXPECT_NE(ZX_HANDLE_INVALID, passed_log_sink_socket);
  EXPECT_EQ(ZX_ERR_BAD_HANDLE, zx_handle_close(passed_log_sink_socket));
}

TEST(SyslogTests, test_log_sink_channel_closed_on_reconfigure_fail) {
  EXPECT_LT(FX_LOG_MAX_TAGS, kTooManyTags.size());

  zx::socket local, remote;
  EXPECT_EQ(ZX_OK, zx::socket::create(ZX_SOCKET_DATAGRAM, &local, &remote));
  const zx_handle_t passed_handle = remote.release();

  fx_logger_config_t config = {.min_severity = FX_LOG_INFO,
                               .console_fd = -1,
                               .log_sink_channel = passed_handle,
                               .log_sink_socket = ZX_HANDLE_INVALID,
                               .tags = kTooManyTags.data(),
                               .num_tags = kTooManyTags.size()};

  // This should fail because there are too many tags, and closing the
  // handle should fail because it is already closed.
  EXPECT_EQ(ZX_ERR_INVALID_ARGS, fx_log_reconfigure(&config));
  EXPECT_NE(ZX_HANDLE_INVALID, passed_handle);
  EXPECT_EQ(ZX_ERR_BAD_HANDLE, zx_handle_close(passed_handle));
}

TEST(SyslogTests, test_log_sink_socket_closed_on_reconfigure_fail) {
  EXPECT_LT(FX_LOG_MAX_TAGS, kTooManyTags.size());

  zx::socket local, remote;
  EXPECT_EQ(ZX_OK, zx::socket::create(ZX_SOCKET_DATAGRAM, &local, &remote));
  const zx_handle_t passed_handle = remote.release();

  fx_logger_config_t config = {.min_severity = FX_LOG_INFO,
                               .console_fd = -1,
                               .log_sink_channel = ZX_HANDLE_INVALID,
                               .log_sink_socket = passed_handle,
                               .tags = kTooManyTags.data(),
                               .num_tags = kTooManyTags.size()};

  // This should fail because there are too many tags, and closing the
  // handle should fail because it is already closed.
  EXPECT_EQ(ZX_ERR_INVALID_ARGS, fx_log_reconfigure(&config));
  EXPECT_NE(ZX_HANDLE_INVALID, passed_handle);
  EXPECT_EQ(ZX_ERR_BAD_HANDLE, zx_handle_close(passed_handle));
}

TEST(SyslogTests, test_both_handles_specified_fails_reconfigure_and_handles_closed) {
  zx::socket local, remote;
  EXPECT_EQ(ZX_OK, zx::socket::create(ZX_SOCKET_DATAGRAM, &local, &remote));
  const zx_handle_t passed_log_sink_channel = local.release();
  const zx_handle_t passed_log_sink_socket = remote.release();

  fx_logger_config_t config = {.min_severity = FX_LOG_INFO,
                               .console_fd = -1,
                               .log_sink_channel = passed_log_sink_channel,
                               .log_sink_socket = passed_log_sink_socket,
                               .tags = nullptr,
                               .num_tags = 0};

  // This should fail because both handles were specified, and closing the
  // handle should fail because it is already closed.
  EXPECT_EQ(ZX_ERR_INVALID_ARGS, fx_log_reconfigure(&config));
  EXPECT_NE(ZX_HANDLE_INVALID, passed_log_sink_channel);
  EXPECT_EQ(ZX_ERR_BAD_HANDLE, zx_handle_close(passed_log_sink_channel));
  EXPECT_NE(ZX_HANDLE_INVALID, passed_log_sink_socket);
  EXPECT_EQ(ZX_ERR_BAD_HANDLE, zx_handle_close(passed_log_sink_socket));
}

#ifdef SYSLOG_STATIC
TEST(SyslogTests, test_create_with_log_sink_channel_not_supported) {
  zx::socket local, remote;
  EXPECT_EQ(ZX_OK, zx::socket::create(ZX_SOCKET_DATAGRAM, &local, &remote));
  const zx_handle_t passed_handle = remote.release();

  fx_logger_config_t config = {.min_severity = FX_LOG_INFO,
                               .console_fd = -1,
                               .log_sink_channel = passed_handle,
                               .log_sink_socket = ZX_HANDLE_INVALID,
                               .tags = nullptr,
                               .num_tags = 0};

  // This should fail because log_sink_channel is not supported, and closing the
  // handle should fail because it is already closed.
  fx_logger_t* logger;
  EXPECT_EQ(ZX_ERR_INVALID_ARGS, fx_logger_create(&config, &logger));
  EXPECT_NE(ZX_HANDLE_INVALID, passed_handle);
  EXPECT_EQ(ZX_ERR_BAD_HANDLE, zx_handle_close(passed_handle));
}

TEST(SyslogTests, test_reconfigure_with_log_sink_channel_not_supported) {
  zx::socket local, remote;
  EXPECT_EQ(ZX_OK, zx::socket::create(ZX_SOCKET_DATAGRAM, &local, &remote));
  const zx_handle_t passed_handle = remote.release();

  fx_logger_config_t config = {.min_severity = FX_LOG_INFO,
                               .console_fd = -1,
                               .log_sink_channel = passed_handle,
                               .log_sink_socket = ZX_HANDLE_INVALID,
                               .tags = nullptr,
                               .num_tags = 0};

  // This should fail because log_sink_channel is not supported, and closing the
  // handle should fail because it is already closed.
  EXPECT_EQ(ZX_ERR_INVALID_ARGS, fx_log_reconfigure(&config));
  EXPECT_NE(ZX_HANDLE_INVALID, passed_handle);
  EXPECT_EQ(ZX_ERR_BAD_HANDLE, zx_handle_close(passed_handle));
}
#endif
