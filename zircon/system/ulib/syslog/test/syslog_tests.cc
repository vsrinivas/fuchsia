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

#include <zxtest/zxtest.h>

static bool ends_with(const char* str, const char* suffix) {
  if (strlen(str) < strlen(suffix)) {
    return false;
  }
  size_t l = strlen(suffix);
  str += strlen(str) - l;
  return strcmp(str, suffix) == 0;
}

static void smallest_unused_fd(int* fd) {
  char name[] = "/tmp/syslog_test.XXXXXX";
  *fd = mkstemp(name);
  ASSERT_GT(*fd, -1);
  close(*fd);
  int status = remove(name);
  ASSERT_EQ(0, status);
}

TEST(SyslogTests, test_log_init_with_socket) {
  zx::socket socket0, socket1;
  EXPECT_EQ(ZX_OK, zx::socket::create(0, &socket0, &socket1));
  fx_logger_config_t config = {.min_severity = FX_LOG_INFO,
                               .console_fd = -1,
                               .log_service_channel = socket1.release(),
                               .tags = nullptr,
                               .num_tags = 0};
  EXPECT_EQ(ZX_OK, fx_log_reconfigure(&config), "");
}

TEST(SyslogTests, test_log_enabled_macro) {
  zx::socket socket0, socket1;
  EXPECT_EQ(ZX_OK, zx::socket::create(0, &socket0, &socket1));
  fx_logger_config_t config = {.min_severity = FX_LOG_INFO,
                               .console_fd = -1,
                               .log_service_channel = socket1.release(),
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
                               .log_service_channel = ZX_HANDLE_INVALID,
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
  FX_LOGF(INFO, NULL, "%d, %s", 10, "just some number");
  char buf[256];
  size_t n = read(pipefd[1], buf, sizeof(buf));
  EXPECT_GT(n, 0u, "");
  buf[n] = 0;
  EXPECT_TRUE(ends_with(buf, "INFO: 10, just some number\n"), "%s", buf);
  close(pipefd[1]);
}

TEST(SyslogTests, test_log_preprocessed_message) {
  int pipefd[2];
  EXPECT_NE(pipe2(pipefd, O_NONBLOCK), -1, "");
  EXPECT_EQ(ZX_OK, init_helper(pipefd[0], NULL, 0), "");
  FX_LOG(INFO, NULL, "%d, %s");
  char buf[256];
  size_t n = read(pipefd[1], buf, sizeof(buf));
  EXPECT_GT(n, 0u, "");
  buf[n] = 0;
  EXPECT_TRUE(ends_with(buf, "INFO: %d, %s\n"), "%s", buf);
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
  FX_LOGF(INFO, "tag", "%d, %s", 10, "just some string");
  char buf[256];
  size_t n = read(pipefd[1], buf, sizeof(buf));
  EXPECT_GT(n, 0u, "");
  buf[n] = 0;
  EXPECT_TRUE(ends_with(buf, "[tag] INFO: 10, just some string\n"), "%s", buf);
  close(pipefd[1]);
}

TEST(SyslogTests, test_log_write_with_global_tag) {
  int pipefd[2];
  EXPECT_NE(pipe2(pipefd, O_NONBLOCK), -1, "");
  const char* tags[] = {"gtag"};
  EXPECT_EQ(ZX_OK, init_helper(pipefd[0], tags, 1), "");
  FX_LOGF(INFO, "tag", "%d, %s", 10, "just some string");
  char buf[256];
  size_t n = read(pipefd[1], buf, sizeof(buf));
  EXPECT_GT(n, 0u, "");
  buf[n] = 0;
  EXPECT_TRUE(ends_with(buf, "[gtag, tag] INFO: 10, just some string\n"), "%s", buf);
  close(pipefd[1]);
}

TEST(SyslogTests, test_log_write_with_multi_global_tag) {
  int pipefd[2];
  EXPECT_NE(pipe2(pipefd, O_NONBLOCK), -1, "");
  const char* tags[] = {"gtag", "gtag2"};
  EXPECT_EQ(ZX_OK, init_helper(pipefd[0], tags, 2), "");
  FX_LOGF(INFO, "tag", "%d, %s", 10, "just some string");
  char buf[256];
  size_t n = read(pipefd[1], buf, sizeof(buf));
  EXPECT_GT(n, 0u, "");
  buf[n] = 0;
  EXPECT_TRUE(ends_with(buf, "[gtag, gtag2, tag] INFO: 10, just some string\n"), "%s", buf);
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
  FX_VLOG(5, NULL, "test message");
  char buf[256];
  size_t n = read(pipefd[1], buf, sizeof(buf));
  EXPECT_GT(n, 0u, "");
  buf[n] = 0;
  EXPECT_TRUE(ends_with(buf, "VLOG(5): test message\n"), "%s", buf);
  close(pipefd[1]);
}

TEST(SyslogTests, test_vlog_write) {
  int pipefd[2];
  EXPECT_NE(pipe2(pipefd, O_NONBLOCK), -1, "");
  EXPECT_EQ(ZX_OK, init_helper(pipefd[0], NULL, 0), "");
  FX_LOG_SET_VERBOSITY(1);  // INFO - 1
  FX_VLOGF(1, NULL, "%d, %s", 10, "just some number");
  char buf[256];
  size_t n = read(pipefd[1], buf, sizeof(buf));
  EXPECT_GT(n, 0u, "");
  buf[n] = 0;
  EXPECT_TRUE(ends_with(buf, "VLOG(1): 10, just some number\n"), "%s", buf);
  close(pipefd[1]);
}

TEST(SyslogTests, test_log_reconfiguration) {

  // Initialize with no tags.
  int pipefd[2];
  EXPECT_NE(pipe2(pipefd, O_NONBLOCK), -1, "");
  EXPECT_EQ(ZX_OK, init_helper(pipefd[0], NULL, 0), "");
  FX_LOG(INFO, NULL, "Hi");
  char buf[256];
  size_t n = read(pipefd[1], buf, sizeof(buf));
  EXPECT_GT(n, 0u, "");
  buf[n] = 0;
  EXPECT_TRUE(ends_with(buf, "INFO: Hi\n"), "%s", buf);

  // Now reconfigure the logger and add tags.
  const char* tags[] = {"tag1", "tag2"};
  EXPECT_EQ(ZX_OK, init_helper(-1, tags, 2), "");
  FX_LOG(INFO, NULL, "Hi");
  n = read(pipefd[1], buf, sizeof(buf));
  EXPECT_GT(n, 0u, "");
  buf[n] = 0;
  EXPECT_TRUE(ends_with(buf, "[tag1, tag2] INFO: Hi\n"), "%s", buf);

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
                               .log_service_channel = ZX_HANDLE_INVALID,
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
