// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/feedback/boot_log_checker/reboot_log_handler.h"

#include <lib/async/cpp/executor.h>
#include <lib/fit/result.h>
#include <zircon/errors.h>

#include <memory>

#include "src/developer/feedback/boot_log_checker/metrics_registry.cb.h"
#include "src/developer/feedback/boot_log_checker/tests/stub_crash_reporter.h"
#include "src/developer/feedback/boot_log_checker/tests/stub_network_reachability_provider.h"
#include "src/developer/feedback/testing/gpretty_printers.h"
#include "src/developer/feedback/testing/stubs/stub_cobalt_logger.h"
#include "src/developer/feedback/testing/stubs/stub_cobalt_logger_factory.h"
#include "src/developer/feedback/testing/unit_test_fixture.h"
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

class RebootLogHandlerTest : public UnitTestFixture {
 public:
  RebootLogHandlerTest() : executor_(dispatcher()) {}

 protected:
  void SetUpNetworkReachabilityProvider(
      std::unique_ptr<StubConnectivity> network_reachability_provider) {
    network_reachability_provider_ = std::move(network_reachability_provider);
    if (network_reachability_provider_) {
      InjectServiceProvider(network_reachability_provider_.get());
    }
  }

  void SetUpCrashReporter(std::unique_ptr<StubCrashReporter> crash_reporter) {
    crash_reporter_ = std::move(crash_reporter);
    if (crash_reporter_) {
      InjectServiceProvider(crash_reporter_.get());
    }
  }

  void SetUpLoggerFactory(std::unique_ptr<StubCobaltLoggerFactoryBase> logger_factory) {
    logger_factory_ = std::move(logger_factory);
    if (logger_factory_) {
      InjectServiceProvider(logger_factory_.get());
    }
  }

  void WriteRebootLogContents(const std::string& contents = "ZIRCON KERNEL PANIC") {
    ASSERT_TRUE(tmp_dir_.NewTempFileWithData(contents, &reboot_log_path_));
  }

  fit::result<void> HandleRebootLog(const std::string& filepath) {
    fit::result<void> result;
    executor_.schedule_task(
        feedback::HandleRebootLog(filepath, services()).then([&result](fit::result<void>& res) {
          result = std::move(res);
        }));
    RunLoopUntilIdle();
    return result;
  }

 private:
  async::Executor executor_;

 protected:
  std::unique_ptr<StubConnectivity> network_reachability_provider_;
  std::unique_ptr<StubCrashReporter> crash_reporter_;
  std::unique_ptr<StubCobaltLoggerFactoryBase> logger_factory_;
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
  SetUpNetworkReachabilityProvider(std::make_unique<StubConnectivity>());
  SetUpCrashReporter(std::make_unique<StubCrashReporter>());
  SetUpLoggerFactory(std::make_unique<StubCobaltLoggerFactory>());

  fit::result<void> result = HandleRebootLog(reboot_log_path_);
  EXPECT_EQ(result.state(), kPending);

  network_reachability_provider_->TriggerOnNetworkReachable(true);
  RunLoopUntilIdle();
  EXPECT_EQ(result.state(), kOk);
  EXPECT_STREQ(crash_reporter_->crash_signature().c_str(), "fuchsia-kernel-panic");
  EXPECT_STREQ(crash_reporter_->reboot_log().c_str(), reboot_log.c_str());

  EXPECT_EQ(logger_factory_->LastMetricId(), cobalt_registry::kRebootMetricId);
  EXPECT_EQ(logger_factory_->LastEventCode(),
            cobalt_registry::RebootMetricDimensionReason::KernelPanic);
}

TEST_F(RebootLogHandlerTest, Succeed_OutOfMemoryLogPresent) {
  const std::string reboot_log = "ZIRCON OOM";
  WriteRebootLogContents(reboot_log);
  SetUpNetworkReachabilityProvider(std::make_unique<StubConnectivity>());
  SetUpCrashReporter(std::make_unique<StubCrashReporter>());
  SetUpLoggerFactory(std::make_unique<StubCobaltLoggerFactory>());

  fit::result<void> result = HandleRebootLog(reboot_log_path_);
  EXPECT_EQ(result.state(), kPending);

  network_reachability_provider_->TriggerOnNetworkReachable(true);
  RunLoopUntilIdle();
  EXPECT_EQ(result.state(), kOk);
  EXPECT_STREQ(crash_reporter_->crash_signature().c_str(), "fuchsia-oom");
  EXPECT_STREQ(crash_reporter_->reboot_log().c_str(), reboot_log.c_str());

  EXPECT_EQ(logger_factory_->LastMetricId(), cobalt_registry::kRebootMetricId);
  EXPECT_EQ(logger_factory_->LastEventCode(), cobalt_registry::RebootMetricDimensionReason::Oom);
}

TEST_F(RebootLogHandlerTest, Succeed_UnrecognizedCrashTypeInRebootLog) {
  const std::string reboot_log = "UNRECOGNIZED CRASH TYPE";
  WriteRebootLogContents(reboot_log);
  SetUpNetworkReachabilityProvider(std::make_unique<StubConnectivity>());
  SetUpCrashReporter(std::make_unique<StubCrashReporter>());
  SetUpLoggerFactory(std::make_unique<StubCobaltLoggerFactory>());

  fit::result<void> result = HandleRebootLog(reboot_log_path_);
  EXPECT_EQ(result.state(), kPending);

  network_reachability_provider_->TriggerOnNetworkReachable(true);
  RunLoopUntilIdle();
  EXPECT_EQ(result.state(), kOk);
  EXPECT_STREQ(crash_reporter_->crash_signature().c_str(), "fuchsia-kernel-panic");
  EXPECT_STREQ(crash_reporter_->reboot_log().c_str(), reboot_log.c_str());

  EXPECT_EQ(logger_factory_->LastMetricId(), cobalt_registry::kRebootMetricId);
  EXPECT_EQ(logger_factory_->LastEventCode(),
            cobalt_registry::RebootMetricDimensionReason::KernelPanic);
}

TEST_F(RebootLogHandlerTest, Pending_NetworkNotReachable) {
  WriteRebootLogContents();
  SetUpNetworkReachabilityProvider(std::make_unique<StubConnectivity>());
  SetUpLoggerFactory(std::make_unique<StubCobaltLoggerFactory>());

  fit::result<void> result = HandleRebootLog(reboot_log_path_);
  EXPECT_EQ(result.state(), kPending);

  network_reachability_provider_->TriggerOnNetworkReachable(false);
  RunLoopUntilIdle();
  EXPECT_EQ(result.state(), kPending);

  network_reachability_provider_->TriggerOnNetworkReachable(false);
  RunLoopUntilIdle();
  EXPECT_EQ(result.state(), kPending);
}

TEST_F(RebootLogHandlerTest, Fail_CallHandleTwice) {
  RebootLogHandler handler(services());
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
  SetUpNetworkReachabilityProvider(nullptr);

  EXPECT_EQ(HandleRebootLog(reboot_log_path_).state(), kError);
}

TEST_F(RebootLogHandlerTest, Fail_NetworkReachabilityProviderClosesConnection) {
  WriteRebootLogContents();
  SetUpNetworkReachabilityProvider(std::make_unique<StubConnectivity>());

  fit::result<void> result = HandleRebootLog(reboot_log_path_);
  EXPECT_EQ(result.state(), kPending);

  network_reachability_provider_->CloseAllConnections();
  RunLoopUntilIdle();
  EXPECT_EQ(result.state(), kError);
}

TEST_F(RebootLogHandlerTest, Fail_CrashReporterNotAvailable) {
  WriteRebootLogContents();
  SetUpNetworkReachabilityProvider(std::make_unique<StubConnectivity>());
  SetUpLoggerFactory(std::make_unique<StubCobaltLoggerFactory>());

  fit::result<void> result = HandleRebootLog(reboot_log_path_);
  EXPECT_EQ(result.state(), kPending);

  network_reachability_provider_->TriggerOnNetworkReachable(true);
  RunLoopUntilIdle();
  EXPECT_EQ(result.state(), kError);
}

TEST_F(RebootLogHandlerTest, Fail_CrashReporterClosesConnection) {
  WriteRebootLogContents();
  SetUpNetworkReachabilityProvider(std::make_unique<StubConnectivity>());
  SetUpCrashReporter(std::make_unique<StubCrashReporterClosesConnection>());
  SetUpLoggerFactory(std::make_unique<StubCobaltLoggerFactory>());

  fit::result<void> result = HandleRebootLog(reboot_log_path_);
  EXPECT_EQ(result.state(), kPending);

  network_reachability_provider_->TriggerOnNetworkReachable(true);
  RunLoopUntilIdle();
  EXPECT_EQ(result.state(), kError);
}

TEST_F(RebootLogHandlerTest, Fail_CrashReporterFailsToFile) {
  WriteRebootLogContents();
  SetUpNetworkReachabilityProvider(std::make_unique<StubConnectivity>());
  SetUpCrashReporter(std::make_unique<StubCrashReporterAlwaysReturnsError>());
  SetUpLoggerFactory(std::make_unique<StubCobaltLoggerFactory>());

  fit::result<void> result = HandleRebootLog(reboot_log_path_);
  EXPECT_EQ(result.state(), kPending);

  network_reachability_provider_->TriggerOnNetworkReachable(true);
  RunLoopUntilIdle();
  EXPECT_EQ(result.state(), kError);
}

TEST_F(RebootLogHandlerTest, Fail_CobaltLoggerNotAvailable) {
  WriteRebootLogContents();
  SetUpNetworkReachabilityProvider(std::make_unique<StubConnectivity>());
  SetUpCrashReporter(std::make_unique<StubCrashReporter>());

  fit::result<void> result = HandleRebootLog(reboot_log_path_);
  EXPECT_EQ(result.state(), kPending);

  network_reachability_provider_->TriggerOnNetworkReachable(true);
  RunLoopUntilIdle();
  EXPECT_EQ(result.state(), kError);
}

TEST_F(RebootLogHandlerTest, Fail_CobaltLoggerClosesConnection) {
  WriteRebootLogContents();
  SetUpNetworkReachabilityProvider(std::make_unique<StubConnectivity>());
  SetUpCrashReporter(std::make_unique<StubCrashReporter>());
  SetUpLoggerFactory(std::make_unique<StubCobaltLoggerFactoryClosesConnection>());

  fit::result<void> result = HandleRebootLog(reboot_log_path_);
  EXPECT_EQ(result.state(), kPending);

  network_reachability_provider_->TriggerOnNetworkReachable(true);
  RunLoopUntilIdle();
  EXPECT_EQ(result.state(), kError);
}

TEST_F(RebootLogHandlerTest, Fail_CobaltLoggerFailsToCreateLogger) {
  WriteRebootLogContents();
  SetUpNetworkReachabilityProvider(std::make_unique<StubConnectivity>());
  SetUpCrashReporter(std::make_unique<StubCrashReporter>());
  SetUpLoggerFactory(std::make_unique<StubCobaltLoggerFactoryFailsToCreateLogger>());

  fit::result<void> result = HandleRebootLog(reboot_log_path_);
  EXPECT_EQ(result.state(), kPending);

  network_reachability_provider_->TriggerOnNetworkReachable(true);
  RunLoopUntilIdle();
  EXPECT_EQ(result.state(), kError);
}

TEST_F(RebootLogHandlerTest, Fail_CobaltLoggerFailsToLogEvent) {
  WriteRebootLogContents();
  SetUpNetworkReachabilityProvider(std::make_unique<StubConnectivity>());
  SetUpCrashReporter(std::make_unique<StubCrashReporter>());
  SetUpLoggerFactory(
      std::make_unique<StubCobaltLoggerFactory>(std::make_unique<StubCobaltLoggerFailsLogEvent>()));

  fit::result<void> result = HandleRebootLog(reboot_log_path_);
  EXPECT_EQ(result.state(), kPending);

  network_reachability_provider_->TriggerOnNetworkReachable(true);
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
