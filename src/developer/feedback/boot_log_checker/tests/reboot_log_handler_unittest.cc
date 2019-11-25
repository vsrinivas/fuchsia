// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/feedback/boot_log_checker/reboot_log_handler.h"

#include <lib/async/cpp/executor.h>
#include <lib/fit/result.h>
#include <lib/zx/time.h>
#include <zircon/errors.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

#include "src/developer/feedback/boot_log_checker/metrics_registry.cb.h"
#include "src/developer/feedback/boot_log_checker/tests/stub_crash_reporter.h"
#include "src/developer/feedback/boot_log_checker/tests/stub_network_reachability_provider.h"
#include "src/developer/feedback/testing/gpretty_printers.h"
#include "src/developer/feedback/testing/stubs/stub_cobalt_logger.h"
#include "src/developer/feedback/testing/stubs/stub_cobalt_logger_factory.h"
#include "src/developer/feedback/testing/unit_test_fixture.h"
#include "src/lib/files/scoped_temp_dir.h"
#include "src/lib/fxl/test/test_settings.h"
#include "src/lib/syslog/cpp/logger.h"
#include "third_party/googletest/googlemock/include/gmock/gmock.h"
#include "third_party/googletest/googletest/include/gtest/gtest.h"

namespace feedback {
namespace {

constexpr fit::result_state kError = fit::result_state::error;
constexpr fit::result_state kOk = fit::result_state::ok;
constexpr fit::result_state kPending = fit::result_state::pending;

constexpr uint32_t kKernelPanic = cobalt_registry::RebootMetricDimensionReason::KernelPanic;
constexpr uint32_t kOom = cobalt_registry::RebootMetricDimensionReason::Oom;

struct TestParam {
  std::string test_name;
  std::string input_reboot_log;
  std::string output_crash_signature;
  std::optional<zx::duration> output_uptime;
  uint32_t output_cobalt_event_code;
};

class RebootLogHandlerTest : public UnitTestFixture, public testing::WithParamInterface<TestParam> {
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

  void WriteRebootLogContents(
      const std::string& contents = "ZIRCON KERNEL PANIC\n\nUPTIME (ms)\n74715002") {
    ASSERT_TRUE(tmp_dir_.NewTempFileWithData(contents, &reboot_log_path_));
  }

  fit::result<void> HandleRebootLog() {
    fit::result<void> result;
    executor_.schedule_task(
        feedback::HandleRebootLog(reboot_log_path_, services())
            .then([&result](fit::result<void>& res) { result = std::move(res); }));
    RunLoopUntilIdle();
    return result;
  }

  fit::result<void> HandleRebootLogTriggerOnNetworkReachable() {
    auto result = HandleRebootLog();
    EXPECT_EQ(result.state(), kPending);

    network_reachability_provider_->TriggerOnNetworkReachable(true);
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
  // We write nothing in |reboot_log_path_| so no file will exist at that path.
  EXPECT_EQ(HandleRebootLog().state(), kOk);
}

INSTANTIATE_TEST_SUITE_P(WithVariousRebootLogs, RebootLogHandlerTest,
                         ::testing::ValuesIn(std::vector<TestParam>({
                             {
                                 "KernelPanicCrashLog",
                                 "ZIRCON KERNEL PANIC\n\nUPTIME (ms)\n74715002",
                                 "fuchsia-kernel-panic",
                                 zx::msec(74715002),
                                 kKernelPanic,
                             },
                             {
                                 "KernelPanicCrashLogNoUptime",
                                 "ZIRCON KERNEL PANIC",
                                 "fuchsia-kernel-panic",
                                 std::nullopt,
                                 kKernelPanic,
                             },
                             {
                                 "KernelPanicCrashLogWrongUptime",
                                 "ZIRCON KERNEL PANIC\n\nUNRECOGNIZED",
                                 "fuchsia-kernel-panic",
                                 std::nullopt,
                                 kKernelPanic,
                             },
                             {
                                 "OutOfMemoryLog",
                                 "ZIRCON OOM\n\nUPTIME (ms)\n65487494",
                                 "fuchsia-oom",
                                 zx::msec(65487494),
                                 kOom,
                             },
                             {
                                 "OutOfMemoryLogNoUptime",
                                 "ZIRCON OOM",
                                 "fuchsia-oom",
                                 std::nullopt,
                                 kOom,
                             },
                             {
                                 "UnrecognizedCrashTypeInRebootLog",
                                 "UNRECOGNIZED CRASH TYPE",
                                 "fuchsia-kernel-panic",
                                 std::nullopt,
                                 kKernelPanic,
                             },
                         })),
                         [](const testing::TestParamInfo<TestParam>& info) {
                           return info.param.test_name;
                         });

TEST_P(RebootLogHandlerTest, Succeed) {
  const auto param = GetParam();

  WriteRebootLogContents(param.input_reboot_log);
  SetUpNetworkReachabilityProvider(std::make_unique<StubConnectivity>());
  SetUpCrashReporter(std::make_unique<StubCrashReporter>());
  SetUpLoggerFactory(std::make_unique<StubCobaltLoggerFactory>());

  fit::result<void> result = HandleRebootLogTriggerOnNetworkReachable();
  EXPECT_EQ(result.state(), kOk);
  EXPECT_STREQ(crash_reporter_->crash_signature().c_str(), param.output_crash_signature.c_str());
  EXPECT_STREQ(crash_reporter_->reboot_log().c_str(), param.input_reboot_log.c_str());
  EXPECT_EQ(crash_reporter_->uptime(), param.output_uptime);

  EXPECT_EQ(logger_factory_->LastMetricId(), cobalt_registry::kRebootMetricId);
  EXPECT_EQ(logger_factory_->LastEventCode(), param.output_cobalt_event_code);
}

TEST_F(RebootLogHandlerTest, Pending_NetworkNotReachable) {
  WriteRebootLogContents();
  SetUpNetworkReachabilityProvider(std::make_unique<StubConnectivity>());
  SetUpLoggerFactory(std::make_unique<StubCobaltLoggerFactory>());

  fit::result<void> result = HandleRebootLog();
  EXPECT_EQ(result.state(), kPending);

  network_reachability_provider_->TriggerOnNetworkReachable(false);
  RunLoopUntilIdle();
  EXPECT_EQ(result.state(), kPending);

  network_reachability_provider_->TriggerOnNetworkReachable(false);
  RunLoopUntilIdle();
  EXPECT_EQ(result.state(), kPending);
}

TEST_F(RebootLogHandlerTest, Fail_EmptyRebootLog) {
  WriteRebootLogContents("");
  EXPECT_EQ(HandleRebootLog().state(), kError);
}

TEST_F(RebootLogHandlerTest, Fail_NetworkReachabilityProviderNotAvailable) {
  WriteRebootLogContents();
  SetUpNetworkReachabilityProvider(nullptr);

  EXPECT_EQ(HandleRebootLog().state(), kError);
}

TEST_F(RebootLogHandlerTest, Fail_NetworkReachabilityProviderClosesConnection) {
  WriteRebootLogContents();
  SetUpNetworkReachabilityProvider(std::make_unique<StubConnectivity>());

  fit::result<void> result = HandleRebootLog();
  EXPECT_EQ(result.state(), kPending);

  network_reachability_provider_->CloseAllConnections();
  RunLoopUntilIdle();
  EXPECT_EQ(result.state(), kError);
}

TEST_F(RebootLogHandlerTest, Fail_CrashReporterNotAvailable) {
  WriteRebootLogContents();
  SetUpNetworkReachabilityProvider(std::make_unique<StubConnectivity>());
  SetUpLoggerFactory(std::make_unique<StubCobaltLoggerFactory>());

  fit::result<void> result = HandleRebootLogTriggerOnNetworkReachable();
  EXPECT_EQ(result.state(), kError);
}

TEST_F(RebootLogHandlerTest, Fail_CrashReporterClosesConnection) {
  WriteRebootLogContents();
  SetUpNetworkReachabilityProvider(std::make_unique<StubConnectivity>());
  SetUpCrashReporter(std::make_unique<StubCrashReporterClosesConnection>());
  SetUpLoggerFactory(std::make_unique<StubCobaltLoggerFactory>());

  fit::result<void> result = HandleRebootLogTriggerOnNetworkReachable();
  EXPECT_EQ(result.state(), kError);
}

TEST_F(RebootLogHandlerTest, Fail_CrashReporterFailsToFile) {
  WriteRebootLogContents();
  SetUpNetworkReachabilityProvider(std::make_unique<StubConnectivity>());
  SetUpCrashReporter(std::make_unique<StubCrashReporterAlwaysReturnsError>());
  SetUpLoggerFactory(std::make_unique<StubCobaltLoggerFactory>());

  fit::result<void> result = HandleRebootLogTriggerOnNetworkReachable();
  EXPECT_EQ(result.state(), kError);
}

TEST_F(RebootLogHandlerTest, Fail_CobaltLoggerNotAvailable) {
  WriteRebootLogContents();
  SetUpNetworkReachabilityProvider(std::make_unique<StubConnectivity>());
  SetUpCrashReporter(std::make_unique<StubCrashReporter>());

  fit::result<void> result = HandleRebootLogTriggerOnNetworkReachable();
  EXPECT_EQ(result.state(), kError);
}

TEST_F(RebootLogHandlerTest, Fail_CobaltLoggerClosesConnection) {
  WriteRebootLogContents();
  SetUpNetworkReachabilityProvider(std::make_unique<StubConnectivity>());
  SetUpCrashReporter(std::make_unique<StubCrashReporter>());
  SetUpLoggerFactory(std::make_unique<StubCobaltLoggerFactoryClosesConnection>());

  fit::result<void> result = HandleRebootLogTriggerOnNetworkReachable();
  EXPECT_EQ(result.state(), kError);
}

TEST_F(RebootLogHandlerTest, Fail_CobaltLoggerFailsToCreateLogger) {
  WriteRebootLogContents();
  SetUpNetworkReachabilityProvider(std::make_unique<StubConnectivity>());
  SetUpCrashReporter(std::make_unique<StubCrashReporter>());
  SetUpLoggerFactory(std::make_unique<StubCobaltLoggerFactoryFailsToCreateLogger>());

  fit::result<void> result = HandleRebootLogTriggerOnNetworkReachable();
  EXPECT_EQ(result.state(), kError);
}

TEST_F(RebootLogHandlerTest, Fail_CobaltLoggerFailsToLogEvent) {
  WriteRebootLogContents();
  SetUpNetworkReachabilityProvider(std::make_unique<StubConnectivity>());
  SetUpCrashReporter(std::make_unique<StubCrashReporter>());
  SetUpLoggerFactory(
      std::make_unique<StubCobaltLoggerFactory>(std::make_unique<StubCobaltLoggerFailsLogEvent>()));

  fit::result<void> result = HandleRebootLogTriggerOnNetworkReachable();
  EXPECT_EQ(result.state(), kError);
}

TEST_F(RebootLogHandlerTest, Fail_CallHandleTwice) {
  internal::RebootLogHandler handler(services());
  handler.Handle("irrelevant");
  ASSERT_DEATH(handler.Handle("irrelevant"),
               testing::HasSubstr("Handle() is not intended to be called twice"));
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
