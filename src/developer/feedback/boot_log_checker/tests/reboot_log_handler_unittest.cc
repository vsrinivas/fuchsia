// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/feedback/boot_log_checker/reboot_log_handler.h"

#include <lib/async/cpp/executor.h>
#include <lib/fit/result.h>
#include <lib/fit/single_threaded_executor.h>
#include <lib/gtest/test_loop_fixture.h>
#include <lib/sys/cpp/testing/service_directory_provider.h>
#include <zircon/errors.h>

#include <memory>

#include "src/developer/feedback/boot_log_checker/metrics_registry.cb.h"
#include "src/developer/feedback/boot_log_checker/tests/stub_crash_reporter.h"
#include "src/developer/feedback/boot_log_checker/tests/stub_network_reachability_provider.h"
#include "src/developer/feedback/testing/gpretty_printers.h"
#include "src/developer/feedback/testing/stubs/stub_cobalt_logger_factory.h"
#include "src/lib/files/scoped_temp_dir.h"
#include "src/lib/fxl/logging.h"
#include "src/lib/fxl/test/test_settings.h"
#include "src/lib/syslog/cpp/logger.h"
#include "third_party/googletest/googlemock/include/gmock/gmock.h"
#include "third_party/googletest/googletest/include/gtest/gtest.h"

namespace feedback {
namespace {

constexpr fit::result_state kError = fit::result_state::error;
constexpr fit::result_state kOk = fit::result_state::ok;
constexpr fit::result_state kPending = fit::result_state::pending;

class RebootLogHandlerTest : public gtest::TestLoopFixture {
 public:
  RebootLogHandlerTest() : executor_(dispatcher()), service_directory_provider_(dispatcher()) {}

 protected:
  void ResetNetworkReachabilityProvider(
      std::unique_ptr<StubConnectivity> stub_network_reachability_provider) {
    stub_network_reachability_provider_ = std::move(stub_network_reachability_provider);
    if (stub_network_reachability_provider_) {
      FXL_CHECK(service_directory_provider_.AddService(
                    stub_network_reachability_provider_->GetHandler()) == ZX_OK);
    }
  }

  void ResetCrashReporter(std::unique_ptr<StubCrashReporter> stub_crash_reporter) {
    stub_crash_reporter_ = std::move(stub_crash_reporter);
    if (stub_crash_reporter_) {
      FXL_CHECK(service_directory_provider_.AddService(stub_crash_reporter_->GetHandler()) ==
                ZX_OK);
    }
  }

  void ResetLoggerFactory(std::unique_ptr<StubCobaltLoggerFactory> stub_logger_factory) {
    stub_logger_factory_ = std::move(stub_logger_factory);
    if (stub_logger_factory_) {
      FXL_CHECK(service_directory_provider_.AddService(stub_logger_factory_->GetHandler()) ==
                ZX_OK);
    }
  }

  void WriteRebootLogContents(const std::string& contents = "ZIRCON KERNEL PANIC") {
    ASSERT_TRUE(tmp_dir_.NewTempFileWithData(contents, &reboot_log_path_));
  }

  fit::result<void> HandleRebootLog(const std::string& filepath) {
    fit::result<void> result;
    executor_.schedule_task(
        feedback::HandleRebootLog(filepath, service_directory_provider_.service_directory())
            .then([&result](fit::result<void>& res) { result = std::move(res); }));
    RunLoopUntilIdle();
    return result;
  }

 private:
  async::Executor executor_;

 protected:
  sys::testing::ServiceDirectoryProvider service_directory_provider_;
  std::unique_ptr<StubConnectivity> stub_network_reachability_provider_;
  std::unique_ptr<StubCrashReporter> stub_crash_reporter_;
  std::unique_ptr<StubCobaltLoggerFactory> stub_logger_factory_;
  std::string reboot_log_path_;

 private:
  files::ScopedTempDir tmp_dir_;
};

TEST_F(RebootLogHandlerTest, Succeed_NoRebootLog) {
  EXPECT_EQ(HandleRebootLog("non-existent/file").state(), kOk);
}

TEST_F(RebootLogHandlerTest, Succeed_KernelPanicCrashLogPresent) {
  const std::string reboot_log = "ZIRCON KERNEL PANIC";
  WriteRebootLogContents(reboot_log);
  ResetNetworkReachabilityProvider(std::make_unique<StubConnectivity>());
  ResetCrashReporter(std::make_unique<StubCrashReporter>());
  ResetLoggerFactory(std::make_unique<StubCobaltLoggerFactory>());

  fit::result<void> result = HandleRebootLog(reboot_log_path_);
  EXPECT_EQ(result.state(), kPending);

  stub_network_reachability_provider_->TriggerOnNetworkReachable(true);
  RunLoopUntilIdle();
  EXPECT_EQ(result.state(), kOk);
  EXPECT_STREQ(stub_crash_reporter_->crash_signature().c_str(), "fuchsia-kernel-panic");
  EXPECT_STREQ(stub_crash_reporter_->reboot_log().c_str(), reboot_log.c_str());

  EXPECT_EQ(stub_logger_factory_->last_metric_id(), cobalt_registry::kRebootMetricId);
  EXPECT_EQ(stub_logger_factory_->last_event_code(),
            cobalt_registry::RebootMetricDimensionReason::KernelPanic);
}

TEST_F(RebootLogHandlerTest, Succeed_OutOfMemoryLogPresent) {
  const std::string reboot_log = "ZIRCON OOM";
  WriteRebootLogContents(reboot_log);
  ResetNetworkReachabilityProvider(std::make_unique<StubConnectivity>());
  ResetCrashReporter(std::make_unique<StubCrashReporter>());
  ResetLoggerFactory(std::make_unique<StubCobaltLoggerFactory>());

  fit::result<void> result = HandleRebootLog(reboot_log_path_);
  EXPECT_EQ(result.state(), kPending);

  stub_network_reachability_provider_->TriggerOnNetworkReachable(true);
  RunLoopUntilIdle();
  EXPECT_EQ(result.state(), kOk);
  EXPECT_STREQ(stub_crash_reporter_->crash_signature().c_str(), "fuchsia-oom");
  EXPECT_STREQ(stub_crash_reporter_->reboot_log().c_str(), reboot_log.c_str());

  EXPECT_EQ(stub_logger_factory_->last_metric_id(), cobalt_registry::kRebootMetricId);
  EXPECT_EQ(stub_logger_factory_->last_event_code(),
            cobalt_registry::RebootMetricDimensionReason::Oom);
}

TEST_F(RebootLogHandlerTest, Succeed_UnrecognizedCrashTypeInRebootLog) {
  const std::string reboot_log = "UNRECOGNIZED CRASH TYPE";
  WriteRebootLogContents(reboot_log);
  ResetNetworkReachabilityProvider(std::make_unique<StubConnectivity>());
  ResetCrashReporter(std::make_unique<StubCrashReporter>());
  ResetLoggerFactory(std::make_unique<StubCobaltLoggerFactory>());

  fit::result<void> result = HandleRebootLog(reboot_log_path_);
  EXPECT_EQ(result.state(), kPending);

  stub_network_reachability_provider_->TriggerOnNetworkReachable(true);
  RunLoopUntilIdle();
  EXPECT_EQ(result.state(), kOk);
  EXPECT_STREQ(stub_crash_reporter_->crash_signature().c_str(), "fuchsia-kernel-panic");
  EXPECT_STREQ(stub_crash_reporter_->reboot_log().c_str(), reboot_log.c_str());

  EXPECT_EQ(stub_logger_factory_->last_metric_id(), cobalt_registry::kRebootMetricId);
  EXPECT_EQ(stub_logger_factory_->last_event_code(),
            cobalt_registry::RebootMetricDimensionReason::KernelPanic);
}

TEST_F(RebootLogHandlerTest, Pending_NetworkNotReachable) {
  WriteRebootLogContents();
  ResetNetworkReachabilityProvider(std::make_unique<StubConnectivity>());
  ResetLoggerFactory(std::make_unique<StubCobaltLoggerFactory>());

  fit::result<void> result = HandleRebootLog(reboot_log_path_);
  EXPECT_EQ(result.state(), kPending);

  stub_network_reachability_provider_->TriggerOnNetworkReachable(false);
  RunLoopUntilIdle();
  EXPECT_EQ(result.state(), kPending);

  stub_network_reachability_provider_->TriggerOnNetworkReachable(false);
  RunLoopUntilIdle();
  EXPECT_EQ(result.state(), kPending);
}

TEST_F(RebootLogHandlerTest, Fail_CallHandleTwice) {
  RebootLogHandler handler(service_directory_provider_.service_directory());
  handler.Handle("irrelevant");
  ASSERT_DEATH(handler.Handle("irrelevant"),
               testing::HasSubstr("Handle() is not intended to be called twice"));
}

TEST_F(RebootLogHandlerTest, Fail_EmptyRebootLog) {
  WriteRebootLogContents("");
  EXPECT_EQ(HandleRebootLog(reboot_log_path_).state(), kError);
}

TEST_F(RebootLogHandlerTest, Fail_NetworkReachabilityProviderNotAvailable) {
  WriteRebootLogContents();
  ResetNetworkReachabilityProvider(nullptr);

  EXPECT_EQ(HandleRebootLog(reboot_log_path_).state(), kError);
}

TEST_F(RebootLogHandlerTest, Fail_NetworkReachabilityProviderClosesConnection) {
  WriteRebootLogContents();
  ResetNetworkReachabilityProvider(std::make_unique<StubConnectivity>());

  fit::result<void> result = HandleRebootLog(reboot_log_path_);
  EXPECT_EQ(result.state(), kPending);

  stub_network_reachability_provider_->CloseAllConnections();
  RunLoopUntilIdle();
  EXPECT_EQ(result.state(), kError);
}

TEST_F(RebootLogHandlerTest, Fail_CrashReporterNotAvailable) {
  WriteRebootLogContents();
  ResetNetworkReachabilityProvider(std::make_unique<StubConnectivity>());
  ResetLoggerFactory(std::make_unique<StubCobaltLoggerFactory>());

  fit::result<void> result = HandleRebootLog(reboot_log_path_);
  EXPECT_EQ(result.state(), kPending);

  stub_network_reachability_provider_->TriggerOnNetworkReachable(true);
  RunLoopUntilIdle();
  EXPECT_EQ(result.state(), kError);
}

TEST_F(RebootLogHandlerTest, Fail_CrashReporterClosesConnection) {
  WriteRebootLogContents();
  ResetNetworkReachabilityProvider(std::make_unique<StubConnectivity>());
  ResetCrashReporter(std::make_unique<StubCrashReporterClosesConnection>());
  ResetLoggerFactory(std::make_unique<StubCobaltLoggerFactory>());

  fit::result<void> result = HandleRebootLog(reboot_log_path_);
  EXPECT_EQ(result.state(), kPending);

  stub_network_reachability_provider_->TriggerOnNetworkReachable(true);
  RunLoopUntilIdle();
  EXPECT_EQ(result.state(), kError);
}

TEST_F(RebootLogHandlerTest, Fail_CrashReporterFailsToFile) {
  WriteRebootLogContents();
  ResetNetworkReachabilityProvider(std::make_unique<StubConnectivity>());
  ResetCrashReporter(std::make_unique<StubCrashReporterAlwaysReturnsError>());
  ResetLoggerFactory(std::make_unique<StubCobaltLoggerFactory>());

  fit::result<void> result = HandleRebootLog(reboot_log_path_);
  EXPECT_EQ(result.state(), kPending);

  stub_network_reachability_provider_->TriggerOnNetworkReachable(true);
  RunLoopUntilIdle();
  EXPECT_EQ(result.state(), kError);
}

TEST_F(RebootLogHandlerTest, Fail_CobaltLoggerNotAvailable) {
  WriteRebootLogContents();
  ResetNetworkReachabilityProvider(std::make_unique<StubConnectivity>());
  ResetCrashReporter(std::make_unique<StubCrashReporter>());

  fit::result<void> result = HandleRebootLog(reboot_log_path_);
  EXPECT_EQ(result.state(), kPending);

  stub_network_reachability_provider_->TriggerOnNetworkReachable(true);
  RunLoopUntilIdle();
  EXPECT_EQ(result.state(), kError);
}

TEST_F(RebootLogHandlerTest, Fail_CobaltLoggerClosesConnection) {
  WriteRebootLogContents();
  ResetNetworkReachabilityProvider(std::make_unique<StubConnectivity>());
  ResetCrashReporter(std::make_unique<StubCrashReporter>());
  ResetLoggerFactory(
      std::make_unique<StubCobaltLoggerFactory>(StubCobaltLoggerFactory::FAIL_CLOSE_CONNECTIONS));

  fit::result<void> result = HandleRebootLog(reboot_log_path_);
  EXPECT_EQ(result.state(), kPending);

  stub_network_reachability_provider_->TriggerOnNetworkReachable(true);
  RunLoopUntilIdle();
  EXPECT_EQ(result.state(), kError);
}

TEST_F(RebootLogHandlerTest, Fail_CobaltLoggerFailsToCreateLogger) {
  WriteRebootLogContents();
  ResetNetworkReachabilityProvider(std::make_unique<StubConnectivity>());
  ResetCrashReporter(std::make_unique<StubCrashReporter>());
  ResetLoggerFactory(
      std::make_unique<StubCobaltLoggerFactory>(StubCobaltLoggerFactory::FAIL_CREATE_LOGGER));

  fit::result<void> result = HandleRebootLog(reboot_log_path_);
  EXPECT_EQ(result.state(), kPending);

  stub_network_reachability_provider_->TriggerOnNetworkReachable(true);
  RunLoopUntilIdle();
  EXPECT_EQ(result.state(), kError);
}

TEST_F(RebootLogHandlerTest, Fail_CobaltLoggerFailsToLogEvent) {
  WriteRebootLogContents();
  ResetNetworkReachabilityProvider(std::make_unique<StubConnectivity>());
  ResetCrashReporter(std::make_unique<StubCrashReporter>());
  ResetLoggerFactory(
      std::make_unique<StubCobaltLoggerFactory>(StubCobaltLoggerFactory::FAIL_LOG_EVENT));

  fit::result<void> result = HandleRebootLog(reboot_log_path_);
  EXPECT_EQ(result.state(), kPending);

  stub_network_reachability_provider_->TriggerOnNetworkReachable(true);
  RunLoopUntilIdle();
  EXPECT_EQ(result.state(), kError);
}

}  // namespace
}  // namespace feedback

int main(int argc, char** argv) {
  if (!fxl::SetTestSettings(argc, argv)) {
    return EXIT_FAILURE;
  }

  testing::InitGoogleTest(&argc, argv);
  syslog::InitLogger({"feedback", "test"});
  return RUN_ALL_TESTS();
}
