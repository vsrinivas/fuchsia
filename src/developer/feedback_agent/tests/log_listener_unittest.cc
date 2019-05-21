// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/feedback_agent/log_listener.h"

#include <fuchsia/mem/cpp/fidl.h>
#include <lib/async_promise/executor.h>
#include <lib/fit/single_threaded_executor.h>
#include <lib/fsl/vmo/strings.h>
#include <lib/gtest/real_loop_fixture.h>
#include <lib/sys/cpp/testing/service_directory_provider.h>
#include <lib/syslog/cpp/logger.h>
#include <lib/syslog/logger.h>
#include <lib/zx/time.h>
#include <zircon/errors.h>

#include <memory>
#include <ostream>
#include <vector>

#include "src/developer/feedback_agent/tests/stub_logger.h"
#include "src/lib/fxl/logging.h"
#include "src/lib/fxl/test/test_settings.h"
#include "third_party/googletest/googlemock/include/gmock/gmock.h"
#include "third_party/googletest/googletest/include/gtest/gtest.h"

namespace fuchsia {
namespace feedback {
namespace {

template <typename ResultListenerT>
bool DoStringBufferMatch(const fuchsia::mem::Buffer& actual,
                         const std::string& expected,
                         ResultListenerT* result_listener) {
  std::string actual_value;
  if (!fsl::StringFromVmo(actual, &actual_value)) {
    *result_listener << "Cannot parse actual VMO to string";
    return false;
  }

  if (actual_value.compare(expected) != 0) {
    return false;
  }

  return true;
}

// Returns true if gMock str(|arg|) matches |expected|.
MATCHER_P(MatchesStringBuffer, expected, "'" + std::string(expected) + "'") {
  return DoStringBufferMatch(arg, expected, result_listener);
}

class CollectSystemLogTest : public gtest::RealLoopFixture {
 public:
  CollectSystemLogTest()
      : executor_(dispatcher()),
        service_directory_provider_loop_(&kAsyncLoopConfigNoAttachToThread),
        service_directory_provider_(
            service_directory_provider_loop_.dispatcher()) {
    // We run the service directory provider in a different loop and thread so
    // that the stub logger can sleep (blocking call) without affecting the main
    // loop.
    FXL_CHECK(service_directory_provider_loop_.StartThread(
                  "service directory provider thread") == ZX_OK);
  }

  ~CollectSystemLogTest() { service_directory_provider_loop_.Shutdown(); }

 protected:
  void ResetStubLogger(std::unique_ptr<StubLogger> stub_logger) {
    stub_logger_ = std::move(stub_logger);
    if (stub_logger_) {
      FXL_CHECK(service_directory_provider_.AddService(stub_logger_->GetHandler(
                    service_directory_provider_loop_.dispatcher())) == ZX_OK);
    }
  }

  fit::result<fuchsia::mem::Buffer> CollectSystemLog(
      zx::duration timeout = zx::sec(1)) {
    fit::result<fuchsia::mem::Buffer> result;
    executor_.schedule_task(
        fuchsia::feedback::CollectSystemLog(
            service_directory_provider_.service_directory(), timeout)
            .then([&result](fit::result<fuchsia::mem::Buffer>& res) {
              result = std::move(res);
            }));
    RunLoopUntil([&result] { return !!result; });
    return result;
  }

 private:
  async::Executor executor_;
  async::Loop service_directory_provider_loop_;
  ::sys::testing::ServiceDirectoryProvider service_directory_provider_;

  std::unique_ptr<StubLogger> stub_logger_;
};

TEST_F(CollectSystemLogTest, Succeed_BasicCase) {
  std::unique_ptr<StubLogger> stub_logger = std::make_unique<StubLogger>();
  stub_logger->set_messages({
      BuildLogMessage(FX_LOG_INFO, "line 1"),
      BuildLogMessage(FX_LOG_WARNING, "line 2", zx::msec(1)),
      BuildLogMessage(FX_LOG_ERROR, "line 3", zx::msec(2)),
      BuildLogMessage(FX_LOG_FATAL, "line 4", zx::msec(3)),
      BuildLogMessage(-1 /*VLOG(1)*/, "line 5", zx::msec(4)),
      BuildLogMessage(-2 /*VLOG(2)*/, "line 6", zx::msec(5)),
      BuildLogMessage(FX_LOG_INFO, "line 7", zx::msec(6), /*tags=*/{"foo"}),
      BuildLogMessage(FX_LOG_INFO, "line 8", zx::msec(7), /*tags=*/{"bar"}),
      BuildLogMessage(FX_LOG_INFO, "line 9", zx::msec(8),
                      /*tags=*/{"foo", "bar"}),
  });
  ResetStubLogger(std::move(stub_logger));

  fit::result<fuchsia::mem::Buffer> result = CollectSystemLog();

  ASSERT_TRUE(result.is_ok());
  fuchsia::mem::Buffer logs = result.take_value();
  EXPECT_THAT(logs, MatchesStringBuffer(
                        R"([15604.000][07559][07687][] INFO: line 1
[15604.001][07559][07687][] WARN: line 2
[15604.002][07559][07687][] ERROR: line 3
[15604.003][07559][07687][] FATAL: line 4
[15604.004][07559][07687][] VLOG(1): line 5
[15604.005][07559][07687][] VLOG(2): line 6
[15604.006][07559][07687][foo] INFO: line 7
[15604.007][07559][07687][bar] INFO: line 8
[15604.008][07559][07687][foo, bar] INFO: line 9
)"));
}

TEST_F(CollectSystemLogTest, Succeed_LoggerUnbindsAfterOneMessage) {
  std::unique_ptr<StubLogger> stub_logger =
      std::make_unique<StubLoggerUnbindsAfterOneMessage>();
  stub_logger->set_messages({
      BuildLogMessage(FX_LOG_INFO,
                      "this line should appear in the partial logs"),
      BuildLogMessage(FX_LOG_INFO,
                      "this line should be missing from the partial logs"),
  });
  ResetStubLogger(std::move(stub_logger));

  fit::result<fuchsia::mem::Buffer> result = CollectSystemLog();

  ASSERT_TRUE(result.is_ok());
  fuchsia::mem::Buffer logs = result.take_value();
  EXPECT_THAT(logs,
              MatchesStringBuffer("[15604.000][07559][07687][] INFO: this line "
                                  "should appear in the partial logs\n"));
}

TEST_F(CollectSystemLogTest, Succeed_LogCollectionTimesOut) {
  // The logger will sleep after the first message and longer than the log
  // collection timeout, resulting in partial logs.
  const zx::duration logger_sleep = zx::sec(1);
  const zx::duration log_collection_timeout = zx::msec(500);

  std::unique_ptr<StubLogger> stub_logger =
      std::make_unique<StubLoggerSleepsAfterOneMessage>(logger_sleep);
  stub_logger->set_messages({
      BuildLogMessage(FX_LOG_INFO,
                      "this line should appear in the partial logs"),
      BuildLogMessage(FX_LOG_INFO,
                      "this line should be missing from the partial logs"),
  });
  ResetStubLogger(std::move(stub_logger));

  fit::result<fuchsia::mem::Buffer> result =
      CollectSystemLog(log_collection_timeout);

  ASSERT_TRUE(result.is_ok());
  fuchsia::mem::Buffer logs = result.take_value();
  EXPECT_THAT(logs,
              MatchesStringBuffer("[15604.000][07559][07687][] INFO: this line "
                                  "should appear in the partial logs\n"));
}

TEST_F(CollectSystemLogTest, Fail_EmptyLog) {
  ResetStubLogger(std::make_unique<StubLogger>());

  fit::result<fuchsia::mem::Buffer> result = CollectSystemLog();

  ASSERT_TRUE(result.is_error());
}

TEST_F(CollectSystemLogTest, Fail_LoggerNotAvailable) {
  ResetStubLogger(nullptr);

  fit::result<fuchsia::mem::Buffer> result = CollectSystemLog();

  ASSERT_TRUE(result.is_error());
}

TEST_F(CollectSystemLogTest, Fail_LoggerNeverBindsToLogListener) {
  ResetStubLogger(std::make_unique<StubLoggerNeverBindsToLogListener>());

  fit::result<fuchsia::mem::Buffer> result = CollectSystemLog();

  ASSERT_TRUE(result.is_error());
}

TEST_F(CollectSystemLogTest, Fail_LoggerNeverCallsLogManyBeforeDone) {
  ResetStubLogger(std::make_unique<StubLoggerNeverCallsLogManyBeforeDone>());

  fit::result<fuchsia::mem::Buffer> result = CollectSystemLog();

  ASSERT_TRUE(result.is_error());
}

}  // namespace
}  // namespace feedback

namespace mem {

// Pretty-prints string VMOs in gTest matchers instead of the default byte
// string in case of failed expectations.
void PrintTo(const Buffer& vmo, std::ostream* os) {
  std::string value;
  FXL_CHECK(fsl::StringFromVmo(vmo, &value));
  *os << "'" << value << "'";
}

}  // namespace mem
}  // namespace fuchsia

int main(int argc, char** argv) {
  if (!fxl::SetTestSettings(argc, argv)) {
    return EXIT_FAILURE;
  }

  testing::InitGoogleTest(&argc, argv);
  syslog::InitLogger({"feedback_agent", "test"});
  return RUN_ALL_TESTS();
}
