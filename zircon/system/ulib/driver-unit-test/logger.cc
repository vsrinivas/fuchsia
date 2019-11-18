// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/driver-unit-test/logger.h>

#include <fbl/algorithm.h>
#include <fbl/alloc_checker.h>
#include <fuchsia/driver/test/c/fidl.h>
#include <lib/fidl/cpp/message.h>
#include <lib/fidl/cpp/message_builder.h>
#include <lib/fidl/txn_header.h>
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
      fbl::min(strlen(log_msg), static_cast<size_t>(fuchsia_driver_test_LOG_MESSAGE_MAX));

  uint32_t len = static_cast<uint32_t>(sizeof(fuchsia_driver_test_LoggerLogMessageRequest) +
                                       FIDL_ALIGN(log_msg_size));

  FIDL_ALIGNDECL char buf[len];
  fidl::Builder builder(buf, len);

  auto* req = builder.New<fuchsia_driver_test_LoggerLogMessageRequest>();
  fidl_init_txn_header(&req->hdr, FIDL_TXID_NO_RESPONSE,
                       fuchsia_driver_test_LoggerLogMessageGenOrdinal);

  auto* data = builder.NewArray<char>(static_cast<uint32_t>(log_msg_size));
  req->msg.data = data;
  req->msg.size = log_msg_size;
  memcpy(data, log_msg, log_msg_size);

  fidl::Message msg(builder.Finalize(), fidl::HandlePart());
  const char* err = nullptr;
  auto status = msg.Encode(&fuchsia_driver_test_LoggerLogMessageRequestTable, &err);
  if (status != ZX_OK) {
    return status;
  }
  return msg.Write(logger->channel_.get(), 0);
}

zx_status_t Logger::SendLogTestCase() {
  size_t test_name_size = fbl::min(strlen(test_case_name_.c_str()),
                                   static_cast<size_t>(fuchsia_driver_test_TEST_CASE_NAME_MAX));

  uint32_t len = static_cast<uint32_t>(sizeof(fuchsia_driver_test_LoggerLogTestCaseRequest) +
                                       FIDL_ALIGN(test_name_size));

  FIDL_ALIGNDECL char buf[len];
  fidl::Builder builder(buf, len);

  auto* req = builder.New<fuchsia_driver_test_LoggerLogTestCaseRequest>();
  fidl_init_txn_header(&req->hdr, FIDL_TXID_NO_RESPONSE,
                       fuchsia_driver_test_LoggerLogTestCaseGenOrdinal);

  auto* data = builder.NewArray<char>(static_cast<uint32_t>(test_name_size));
  req->name.data = data;
  req->name.size = test_name_size;
  memcpy(data, test_case_name_.c_str(), test_name_size);

  req->result.passed = test_case_result_.passed;
  req->result.failed = test_case_result_.failed;
  req->result.skipped = test_case_result_.skipped;

  fidl::Message msg(builder.Finalize(), fidl::HandlePart());
  const char* err = nullptr;
  auto status = msg.Encode(&fuchsia_driver_test_LoggerLogTestCaseRequestTable, &err);
  if (status != ZX_OK) {
    return status;
  }
  return msg.Write(channel_.get(), 0);
}

void Logger::OnTestCaseStart(const zxtest::TestCase& test_case) {
  test_case_name_ = test_case.name();
  test_case_result_.passed = 0;
  test_case_result_.failed = 0;
  test_case_result_.skipped = 0;
}

void Logger::OnTestCaseEnd(const zxtest::TestCase& test_case) { SendLogTestCase(); }

void Logger::OnTestSuccess(const zxtest::TestCase& test_case, const zxtest::TestInfo& test) {
  test_case_result_.passed++;
}

void Logger::OnTestFailure(const zxtest::TestCase& test_case, const zxtest::TestInfo& test) {
  test_case_result_.failed++;
}

void Logger::OnTestSkip(const zxtest::TestCase& test_case, const zxtest::TestInfo& test) {
  test_case_result_.skipped++;
}

}  // namespace driver_unit_test
