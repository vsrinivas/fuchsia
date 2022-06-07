// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.driver.test.logger/cpp/fidl.h>
#include <lib/driver-unit-test/logger.h>
#include <lib/fidl/cpp/message.h>
#include <lib/fidl/cpp/transaction_header.h>

#include <algorithm>

#include <fbl/algorithm.h>
#include <fbl/alloc_checker.h>
#include <zxtest/base/test-case.h>

namespace driver_unit_test {

std::unique_ptr<Logger> Logger::instance_;

zx_status_t Logger::CreateInstance(zx::channel ch) {
  if (!ch) {
    return ZX_ERR_BAD_HANDLE;
  }
  fbl::AllocChecker ac;
  instance_ = std::unique_ptr<Logger>(new (&ac) Logger(std::move(ch)));
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }
  return ZX_OK;
}

// static
zx_status_t Logger::SendLogMessage(const char* log_msg) {
  Logger* logger = GetInstance();
  if (!logger) {
    return ZX_ERR_BAD_STATE;
  }
  size_t log_msg_size =
      std::min(strlen(log_msg), static_cast<size_t>(fuchsia_driver_test_logger::kLogMessageMax));
  fuchsia_driver_test_logger::LoggerLogMessageRequest req;
  req.msg() = std::string(std::string_view(log_msg, log_msg_size));
  fidl::UnownedClientEnd<fuchsia_driver_test_logger::Logger> client_end(logger->channel_.get());
  auto result = fidl::Call(client_end)->LogMessage(req);
  if (result.is_error()) {
    return result.error_value().status();
  }
  return ZX_OK;
}

zx_status_t Logger::SendLogTestCase() {
  size_t test_name_size =
      std::min(strlen(test_case_name_.c_str()),
               static_cast<size_t>(fuchsia_driver_test_logger::kTestCaseNameMax));
  fuchsia_driver_test_logger::LoggerLogTestCaseRequest req;
  req.name() = std::string(std::string_view(test_case_name_.c_str(), test_name_size));
  req.result() = test_case_result_;
  fidl::UnownedClientEnd<fuchsia_driver_test_logger::Logger> client_end(channel_.get());
  auto result = fidl::Call(client_end)->LogTestCase(req);
  if (result.is_error()) {
    return result.error_value().status();
  }
  return ZX_OK;
}

void Logger::OnTestCaseStart(const zxtest::TestCase& test_case) {
  test_case_name_ = test_case.name();
  test_case_result_ = {};
}

void Logger::OnTestCaseEnd(const zxtest::TestCase& test_case) { SendLogTestCase(); }

void Logger::OnTestSuccess(const zxtest::TestCase& test_case, const zxtest::TestInfo& test) {
  test_case_result_.passed()++;
}

void Logger::OnTestFailure(const zxtest::TestCase& test_case, const zxtest::TestInfo& test) {
  test_case_result_.failed()++;
}

void Logger::OnTestSkip(const zxtest::TestCase& test_case, const zxtest::TestInfo& test) {
  test_case_result_.skipped()++;
}

}  // namespace driver_unit_test
