// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/driver-unit-test/logger.h>
#include <lib/fidl/coding.h>
#include <lib/zx/channel.h>
#include <zircon/syscalls.h>

#include <memory>

#include <zxtest/zxtest.h>

namespace {

constexpr size_t kMaxFidlMsgSize = 8096;
constexpr char kLogMessage[] = "test log message";

const char* kFakeTestCaseName = "test_case";
const zxtest::TestCase kFakeTestCase = zxtest::TestCase{
    kFakeTestCaseName, zxtest::Test::SetUpTestCase, zxtest::Test::TearDownTestCase};
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

// Waits for a FIDL message on the channel and populates |out_data| with a pointer to the
// decoded data.
void DecodeMessage(const zx::channel& channel, uint64_t want_ordinal, const fidl_type_t* want_type,
                   std::unique_ptr<uint8_t[]>* out_data, uint32_t* out_data_size) {
  // Verify we receive the expected signal on the channel.
  zx_signals_t pending;
  const zx_signals_t wait = ZX_CHANNEL_READABLE | ZX_CHANNEL_PEER_CLOSED;
  auto deadline = zx::deadline_after(zx::sec(5));
  ASSERT_OK(channel.wait_one(wait, deadline, &pending));
  ASSERT_TRUE(pending & ZX_CHANNEL_READABLE);

  // Create the data buffer and copy the data into it.
  const uint32_t buf_size = kMaxFidlMsgSize;
  std::unique_ptr<uint8_t[]> buf(new uint8_t[buf_size]);
  fidl_incoming_msg_t fidl_msg = {
      .bytes = buf.get(),
      .handles = nullptr,
      .num_bytes = buf_size,
      .num_handles = 0,
  };

  ASSERT_OK(channel.read(0, buf.get(), nullptr /* handles */, buf_size, 0 /* num_handles */,
                         &fidl_msg.num_bytes, nullptr));
  ASSERT_GE(fidl_msg.num_bytes, sizeof(fidl_message_header_t));

  // Decode the message in-place.
  auto* hdr = static_cast<fidl_message_header_t*>(fidl_msg.bytes);
  ASSERT_EQ(want_ordinal, hdr->ordinal);
  ASSERT_OK(fidl_decode_msg(want_type, &fidl_msg, nullptr));

  *out_data = std::move(buf);
  *out_data_size = fidl_msg.num_bytes;
}

TEST_F(LoggerTest, LogMessage) {
  ASSERT_OK(driver_unit_test::Logger::SendLogMessage(kLogMessage));

  std::unique_ptr<uint8_t[]> data_buf;
  uint32_t data_size;
  ASSERT_NO_FATAL_FAILURES(
      DecodeMessage(local_, fuchsia_driver_test_LoggerLogMessageOrdinal,
                    &fuchsia_driver_test_LoggerLogMessageRequestTable, &data_buf, &data_size),
      "could not decode message");
  ASSERT_GE(data_size, sizeof(fuchsia_driver_test_LoggerLogMessageRequest));

  auto request = reinterpret_cast<fuchsia_driver_test_LoggerLogMessageRequest*>(data_buf.get());
  ASSERT_EQ(strlen(kLogMessage), request->msg.size);
  ASSERT_EQ(0, strncmp(kLogMessage, request->msg.data, request->msg.size));
}

// Read and decode the FIDL message from the channel and check that it equals the wanted result.
void ValidateReceivedTestCase(const zx::channel& log_ch,
                              const fuchsia_driver_test_TestCaseResult& want) {
  std::unique_ptr<uint8_t[]> data_buf;
  uint32_t data_size;
  ASSERT_NO_FATAL_FAILURES(
      DecodeMessage(log_ch, fuchsia_driver_test_LoggerLogTestCaseOrdinal,
                    &fuchsia_driver_test_LoggerLogTestCaseRequestTable, &data_buf, &data_size),
      "could not decode message");
  ASSERT_GE(data_size, sizeof(fuchsia_driver_test_LoggerLogTestCaseRequest));

  auto request = reinterpret_cast<fuchsia_driver_test_LoggerLogTestCaseRequest*>(data_buf.get());
  ASSERT_EQ(strlen(kFakeTestCaseName), request->name.size);
  ASSERT_EQ(0, strncmp(kFakeTestCaseName, request->name.data, request->name.size));

  auto got = request->result;
  ASSERT_EQ(want.passed, got.passed);
  ASSERT_EQ(want.failed, got.failed);
  ASSERT_EQ(want.skipped, got.skipped);
}

TEST_F(LoggerTest, LogEmptyTestCase) {
  logger_->OnTestCaseStart(kFakeTestCase);
  logger_->OnTestCaseEnd(kFakeTestCase);

  auto want_result = fuchsia_driver_test_TestCaseResult{};
  ASSERT_NO_FATAL_FAILURES(ValidateReceivedTestCase(local_, want_result));
}

TEST_F(LoggerTest, LogSingleTest) {
  logger_->OnTestCaseStart(kFakeTestCase);
  logger_->OnTestSuccess(kFakeTestCase, kFakeTestInfo);
  logger_->OnTestCaseEnd(kFakeTestCase);

  auto want_result = fuchsia_driver_test_TestCaseResult{};
  want_result.passed = 1;
  ASSERT_NO_FATAL_FAILURES(ValidateReceivedTestCase(local_, want_result));
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

  auto want_result = fuchsia_driver_test_TestCaseResult{};
  want_result.passed = 3;
  want_result.failed = 2;
  want_result.skipped = 1;
  ASSERT_NO_FATAL_FAILURES(ValidateReceivedTestCase(local_, want_result));
}

}  // anonymous namespace
