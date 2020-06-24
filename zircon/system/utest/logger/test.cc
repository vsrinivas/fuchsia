// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <fuchsia/logger/c/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fidl/txn_header.h>
#include <lib/logger/logger.h>
#include <lib/syslog/global.h>

#include <memory>
#include <utility>

#include <zxtest/zxtest.h>

__BEGIN_CDECLS

// This does not come from header file as this function should only be used in
// tests and is not for general use.
void fx_log_reset_global_for_testing(void);

__END_CDECLS
namespace {

bool ends_with(const char* str, const char* suffix) {
  size_t str_len = strlen(str);
  size_t suffix_len = strlen(suffix);
  if (str_len < suffix_len) {
    return false;
  }
  str += str_len - suffix_len;
  return strcmp(str, suffix) == 0;
}

class Fixture {
 public:
  Fixture()
      : fds_valid_(false), loop_(&kAsyncLoopConfigNoAttachToCurrentThread), error_status_(0) {}
  ~Fixture() {
    fx_log_reset_global_for_testing();
    if (fds_valid_) {
      close(pipefd_[0]);
      close(pipefd_[1]);
    }
  }

  zx_status_t error_status() const { return error_status_; }

  void CreateLogger() {
    ASSERT_NE(pipe2(pipefd_, O_NONBLOCK), -1, "");
    zx::channel local, remote;
    ASSERT_EQ(ZX_OK, zx::channel::create(0, &local, &remote));
    logger_ = std::make_unique<logger::LoggerImpl>(std::move(remote), pipefd_[0]);
    ASSERT_EQ(ZX_OK, logger_->Begin(loop_.dispatcher()));
    logger_handle_.reset(local.release());
    logger_->set_error_handler([this](zx_status_t status) { error_status_ = status; });
  }

  void ResetLoggerHandle() { logger_handle_.reset(); }

  void ResetSocket() { socket_.reset(); }

  void ConnectToLogger() {
    ASSERT_TRUE(logger_handle_);
    zx::socket local, remote;
    ASSERT_EQ(ZX_OK, zx::socket::create(ZX_SOCKET_DATAGRAM, &local, &remote));
    fuchsia_logger_LogSinkConnectRequest req;
    memset(&req, 0, sizeof(req));
    fidl_init_txn_header(&req.hdr, 0, fuchsia_logger_LogSinkConnectOrdinal);
    req.socket = FIDL_HANDLE_PRESENT;
    zx_handle_t handles[1] = {remote.release()};
    ASSERT_EQ(ZX_OK, logger_handle_.write(0, &req, sizeof(req), handles, 1));
    loop_.RunUntilIdle();
    socket_.reset(local.release());
  }

  void InitSyslog(const char** tags, size_t ntags) {
    ASSERT_TRUE(socket_);
    fx_logger_config_t config = {.min_severity = FX_LOG_INFO,
                                 .console_fd = -1,
                                 .log_service_channel = socket_.release(),
                                 .tags = tags,
                                 .num_tags = ntags};

    ASSERT_EQ(ZX_OK, fx_log_reconfigure(&config));
  }

  void FullSetup() {
    ASSERT_NO_FATAL_FAILURES(CreateLogger());
    ASSERT_NO_FATAL_FAILURES(ConnectToLogger());
    ASSERT_NO_FATAL_FAILURES(InitSyslog(nullptr, 0));
  }

  void RunLoop() {
    loop_.RunUntilIdle();
    fsync(pipefd_[0]);
  }

  const char* read_buffer() {
    memset(buf_, 0, sizeof(buf_));
    read(pipefd_[1], &buf_, sizeof(buf_));
    return buf_;
  }

 private:
  bool fds_valid_;
  char buf_[4096];  // should be enough for logs
  async::Loop loop_;
  zx_status_t error_status_;
  std::unique_ptr<logger::LoggerImpl> logger_;
  zx::channel logger_handle_;
  zx::socket socket_;
  int pipefd_[2];
};

TEST(LoggerTests, TestLogSimple) {
  Fixture fixture;
  ASSERT_NO_FATAL_FAILURES(fixture.FullSetup());
  FX_LOG(INFO, nullptr, "test_message");
  fixture.RunLoop();
  const char* out = fixture.read_buffer();
  ASSERT_TRUE(ends_with(out, "test_message\n"), "%s", out);
}

TEST(LoggerTests, TestLogMultipleMsgs) {
  Fixture fixture;
  ASSERT_NO_FATAL_FAILURES(fixture.FullSetup());
  FX_LOG(INFO, nullptr, "test_message");
  fixture.RunLoop();
  const char* out = fixture.read_buffer();
  ASSERT_TRUE(ends_with(out, "INFO: test_message\n"), "%s", out);
  FX_LOG(INFO, nullptr, "test_message2");
  fixture.RunLoop();
  out = fixture.read_buffer();
  ASSERT_TRUE(ends_with(out, "INFO: test_message2\n"), "%s", out);
}

TEST(LoggerTests, TestLogWithTag) {
  Fixture fixture;
  ASSERT_NO_FATAL_FAILURES(fixture.FullSetup());
  FX_LOG(INFO, "tag", "test_message");
  fixture.RunLoop();
  const char* out = fixture.read_buffer();
  ASSERT_TRUE(ends_with(out, "[tag] INFO: test_message\n"), "%s", out);
}

TEST(LoggerTests, TestLogWithMultipleTags) {
  Fixture fixture;
  ASSERT_NO_FATAL_FAILURES(fixture.CreateLogger());
  ASSERT_NO_FATAL_FAILURES(fixture.ConnectToLogger());
  const char* gtags[] = {"gtag1", "gtag2"};
  ASSERT_NO_FATAL_FAILURES(fixture.InitSyslog(gtags, 2));
  FX_LOG(INFO, "tag", "test_message");
  fixture.RunLoop();
  const char* out = fixture.read_buffer();
  ASSERT_TRUE(ends_with(out, "[gtag1, gtag2, tag] INFO: test_message\n"), "%s", out);
}

TEST(LoggerTests, TestLogSeverity) {
  Fixture fixture;
  ASSERT_NO_FATAL_FAILURES(fixture.FullSetup());
  FX_LOG(INFO, "", "test_message");
  fixture.RunLoop();
  const char* out = fixture.read_buffer();
  ASSERT_TRUE(ends_with(out, "[] INFO: test_message\n"), "%s", out);

  FX_LOG(WARNING, "", "test_message");
  fixture.RunLoop();
  out = fixture.read_buffer();
  ASSERT_TRUE(ends_with(out, "[] WARNING: test_message\n"), "%s", out);

  FX_LOG(ERROR, "", "test_message");
  fixture.RunLoop();
  out = fixture.read_buffer();
  ASSERT_TRUE(ends_with(out, "[] ERROR: test_message\n"), "%s", out);
}

TEST(LoggerTests, TestLogWhenLoggerHandleDies) {
  Fixture fixture;
  ASSERT_NO_FATAL_FAILURES(fixture.FullSetup());
  fixture.ResetLoggerHandle();
  fixture.RunLoop();
  FX_LOG(INFO, "tag", "test_message");
  fixture.RunLoop();
  const char* out = fixture.read_buffer();
  ASSERT_TRUE(ends_with(out, "[tag] INFO: test_message\n"), "%s", out);
  ASSERT_EQ(ZX_OK, fixture.error_status());
}

TEST(LoggerTests, TestLoggerDiesWithSocket) {
  Fixture fixture;
  ASSERT_NO_FATAL_FAILURES(fixture.CreateLogger());
  ASSERT_NO_FATAL_FAILURES(fixture.ConnectToLogger());
  fixture.ResetSocket();
  fixture.RunLoop();
  ASSERT_EQ(ZX_ERR_PEER_CLOSED, fixture.error_status());
}

TEST(LoggerTests, TestLoggerDiesWithChannelWhenNoConnectCalled) {
  Fixture fixture;
  ASSERT_NO_FATAL_FAILURES(fixture.CreateLogger());
  fixture.RunLoop();
  ASSERT_EQ(ZX_OK, fixture.error_status());
  fixture.ResetLoggerHandle();
  fixture.RunLoop();
  ASSERT_EQ(ZX_ERR_PEER_CLOSED, fixture.error_status());
}

}  // namespace
