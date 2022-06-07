// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.driver.test.logger/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/driver-unit-test/logger.h>
#include <lib/fidl/coding.h>
#include <lib/zx/channel.h>
#include <zircon/syscalls.h>

#include <memory>

#include <zxtest/zxtest.h>

namespace {

constexpr char kLogMessage[] = "test log message";

const char* kFakeTestCaseName = "test_case";
const zxtest::TestCase kFakeTestCase = zxtest::TestCase{
    kFakeTestCaseName, zxtest::Test::SetUpTestSuite, zxtest::Test::TearDownTestSuite};
const zxtest::TestInfo kFakeTestInfo = zxtest::TestInfo("test", zxtest::SourceLocation{}, nullptr);

class LoggerTest : public zxtest::Test {
 protected:
  void SetUp() override {
    zx::channel remote;
    ASSERT_OK(zx::channel::create(0, &local_, &remote));
    ASSERT_EQ(ZX_OK, driver_unit_test::Logger::CreateInstance(std::move(remote)));
    logger_ = driver_unit_test::Logger::GetInstance();
    ASSERT_NOT_NULL(logger_);
  }

  void TearDown() override {
    driver_unit_test::Logger::DeleteInstance();
    ASSERT_NULL(driver_unit_test::Logger::GetInstance());
  }

  zx::channel local_;
  driver_unit_test::Logger* logger_;
};

TEST_F(LoggerTest, CreateWithInvalidChannel) {
  driver_unit_test::Logger::DeleteInstance();
  ASSERT_NULL(driver_unit_test::Logger::GetInstance());

  zx::channel invalid;
  ASSERT_NE(ZX_OK, driver_unit_test::Logger::CreateInstance(std::move(invalid)));
  ASSERT_NULL(driver_unit_test::Logger::GetInstance());
  ASSERT_NE(ZX_OK, driver_unit_test::Logger::SendLogMessage(kLogMessage));
}

struct LoggerTestServer : public fidl::Server<fuchsia_driver_test_logger::Logger> {
  void LogMessage(LogMessageRequest& request, LogMessageCompleter::Sync& completer) override {
    logged_message_ = request.msg();
  }
  void LogTestCase(LogTestCaseRequest& request, LogTestCaseCompleter::Sync& completer) override {
    logged_test_case_name_ = request.name();
    logged_test_case_result_ = request.result();
  }

  std::string logged_message_;

  std::string logged_test_case_name_;
  fuchsia_driver_test_logger::TestCaseResult logged_test_case_result_;
};

TEST_F(LoggerTest, LogMessage) {
  ASSERT_OK(driver_unit_test::Logger::SendLogMessage(kLogMessage));

  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  LoggerTestServer server;
  fidl::ServerEnd<fuchsia_driver_test_logger::Logger> server_end(std::move(local_));
  fidl::BindServer(loop.dispatcher(), std::move(server_end), &server);

  loop.RunUntilIdle();

  ASSERT_EQ(strlen(kLogMessage), server.logged_message_.size());
  ASSERT_EQ(0, strncmp(kLogMessage, server.logged_message_.data(), server.logged_message_.size()));
}

// Read and decode the FIDL message from the channel and check that it equals the wanted result.
void ValidateReceivedTestCase(zx::channel log_ch,
                              const fuchsia_driver_test_logger::TestCaseResult& want) {
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  LoggerTestServer server;
  fidl::ServerEnd<fuchsia_driver_test_logger::Logger> server_end(std::move(log_ch));
  fidl::BindServer(loop.dispatcher(), std::move(server_end), &server);

  loop.RunUntilIdle();

  ASSERT_EQ(strlen(kFakeTestCaseName), server.logged_test_case_name_.size());
  ASSERT_EQ(0, strncmp(kFakeTestCaseName, server.logged_test_case_name_.data(),
                       server.logged_test_case_name_.size()));

  ASSERT_EQ(want.passed(), server.logged_test_case_result_.passed());
  ASSERT_EQ(want.failed(), server.logged_test_case_result_.failed());
  ASSERT_EQ(want.skipped(), server.logged_test_case_result_.skipped());
}

TEST_F(LoggerTest, LogEmptyTestCase) {
  logger_->OnTestCaseStart(kFakeTestCase);
  logger_->OnTestCaseEnd(kFakeTestCase);

  auto want_result = fuchsia_driver_test_logger::TestCaseResult{};
  ASSERT_NO_FATAL_FAILURE(ValidateReceivedTestCase(std::move(local_), want_result));
}

TEST_F(LoggerTest, LogSingleTest) {
  logger_->OnTestCaseStart(kFakeTestCase);
  logger_->OnTestSuccess(kFakeTestCase, kFakeTestInfo);
  logger_->OnTestCaseEnd(kFakeTestCase);

  auto want_result = fuchsia_driver_test_logger::TestCaseResult{};
  want_result.passed() = 1;
  ASSERT_NO_FATAL_FAILURE(ValidateReceivedTestCase(std::move(local_), want_result));
}

TEST_F(LoggerTest, LogMultipleTest) {
  logger_->OnTestCaseStart(kFakeTestCase);
  logger_->OnTestFailure(kFakeTestCase, kFakeTestInfo);
  logger_->OnTestFailure(kFakeTestCase, kFakeTestInfo);
  logger_->OnTestSuccess(kFakeTestCase, kFakeTestInfo);
  logger_->OnTestSkip(kFakeTestCase, kFakeTestInfo);
  logger_->OnTestSuccess(kFakeTestCase, kFakeTestInfo);
  logger_->OnTestSuccess(kFakeTestCase, kFakeTestInfo);
  logger_->OnTestCaseEnd(kFakeTestCase);

  auto want_result = fuchsia_driver_test_logger::TestCaseResult{};
  want_result.passed() = 3;
  want_result.failed() = 2;
  want_result.skipped() = 1;
  ASSERT_NO_FATAL_FAILURE(ValidateReceivedTestCase(std::move(local_), want_result));
}

}  // anonymous namespace
