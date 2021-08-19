// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/modular/testing/cpp/fidl.h>
#include <lib/async/cpp/executor.h>
#include <lib/fpromise/result.h>
#include <lib/inspect/contrib/cpp/archive_reader.h>
#include <lib/inspect/cpp/health.h>
#include <lib/inspect/testing/cpp/inspect.h>
#include <lib/syslog/cpp/macros.h>

#include <gmock/gmock.h>

#include "src/modular/lib/modular_config/modular_config.h"
#include "src/modular/lib/modular_config/modular_config_constants.h"
#include "src/modular/lib/modular_test_harness/cpp/fake_session_shell.h"
#include "src/modular/lib/modular_test_harness/cpp/test_harness_fixture.h"
#include "src/modular/lib/modular_test_harness/cpp/test_harness_impl.h"

namespace {

using ::testing::HasSubstr;

constexpr char kBasemgrSelector[] = "*_inspect/basemgr.cmx:root";
constexpr char kBasemgrName[] = "basemgr.cmx";

class InspectBasemgrTest : public modular_testing::TestHarnessFixture {
 protected:
  InspectBasemgrTest()
      : fake_session_shell_(modular_testing::FakeSessionShell::CreateWithDefaultOptions()),
        executor_(dispatcher()) {}

  void RunHarnessAndInterceptSessionShell() {
    fuchsia::modular::testing::TestHarnessSpec spec;
    spec.set_environment_suffix("inspect");
    modular_testing::TestHarnessBuilder builder(std::move(spec));
    builder.InterceptSessionShell(fake_session_shell_->BuildInterceptOptions());
    builder.BuildAndRun(test_harness());

    // Wait for our session shell to start.
    RunLoopUntil([&] { return fake_session_shell_->is_running(); });
  }

  fpromise::result<inspect::contrib::DiagnosticsData> GetInspectDiagnosticsData() {
    auto archive = real_services()->Connect<fuchsia::diagnostics::ArchiveAccessor>();

    inspect::contrib::ArchiveReader reader(std::move(archive), {kBasemgrSelector});
    fpromise::result<std::vector<inspect::contrib::DiagnosticsData>, std::string> result;
    executor_.schedule_task(
        reader.SnapshotInspectUntilPresent({kBasemgrName})
            .then([&](fpromise::result<std::vector<inspect::contrib::DiagnosticsData>, std::string>&
                          rest) { result = std::move(rest); }));
    RunLoopUntil([&] { return result.is_ok() || result.is_error(); });

    if (result.is_error()) {
      EXPECT_FALSE(result.is_error()) << "Error was " << result.error();
      return fpromise::error();
    }

    if (result.value().size() != 1) {
      EXPECT_EQ(1u, result.value().size()) << "Expected only one component";
      return fpromise::error();
    }

    return fpromise::ok(std::move(result.value()[0]));
  }

  std::unique_ptr<modular_testing::FakeSessionShell> fake_session_shell_;
  async::Executor executor_;
};

// Tests that basemgr exposes its configuration in Inspect.
TEST_F(InspectBasemgrTest, ExposesConfig) {
  RunHarnessAndInterceptSessionShell();

  auto inspect_result = GetInspectDiagnosticsData();
  ASSERT_TRUE(inspect_result.is_ok());
  auto inspect_data = inspect_result.take_value();

  // The inspect property should contain configuration that uses |session_shell|.
  const auto& config_value = inspect_data.GetByPath({"root", modular_config::kInspectConfig});
  ASSERT_TRUE(config_value.IsString());
  EXPECT_THAT(config_value.GetString(), HasSubstr(fake_session_shell_->url()));
}

// Tests that basemgr exposes a fuchsia.inspect.Health entry.
TEST_F(InspectBasemgrTest, Health) {
  RunHarnessAndInterceptSessionShell();

  auto inspect_result = GetInspectDiagnosticsData();
  ASSERT_TRUE(inspect_result.is_ok());
  auto inspect_data = inspect_result.take_value();

  const auto& health_status = inspect_data.GetByPath({"root", inspect::kHealthNodeName, "status"});
  ASSERT_TRUE(health_status.IsString());
  EXPECT_EQ(inspect::kHealthOk, std::string(health_status.GetString()));

  const auto& start_timestamp =
      inspect_data.GetByPath({"root", inspect::kHealthNodeName, inspect::kStartTimestamp});
  ASSERT_TRUE(start_timestamp.IsNumber());
}

// Tests that basemgr exposes a session startup timestamp.
TEST_F(InspectBasemgrTest, SessionStartedAt) {
  RunHarnessAndInterceptSessionShell();

  auto inspect_result = GetInspectDiagnosticsData();
  ASSERT_TRUE(inspect_result.is_ok());
  auto inspect_data = inspect_result.take_value();

  const auto& config_value = inspect_data.GetByPath({"root", "session_started_at", "0", "@time"});
  ASSERT_TRUE(config_value.IsNumber());
}

// Tests that basemgr exposes a second session startup timestamp when the session is restarted.
TEST_F(InspectBasemgrTest, SessionStartedAtRestart) {
  RunHarnessAndInterceptSessionShell();

  {
    auto inspect_result = GetInspectDiagnosticsData();
    ASSERT_TRUE(inspect_result.is_ok());
    auto inspect_data = inspect_result.take_value();

    // Inspect should initially contain one timestamp from the initial session.
    const auto& first_time = inspect_data.GetByPath({"root", "session_started_at", "0", "@time"});
    ASSERT_TRUE(first_time.IsNumber());
  }

  // Restart the session.
  fake_session_shell_->session_shell_context()->Restart();

  // Wait for the session shell to die (indicating a restart), then wait for it to come back.
  RunLoopUntil([&] { return !fake_session_shell_->is_running(); });
  RunLoopUntil([&] { return fake_session_shell_->is_running(); });

  {
    // Read the inspect data again.
    auto inspect_result = GetInspectDiagnosticsData();
    ASSERT_TRUE(inspect_result.is_ok());
    auto inspect_data = inspect_result.take_value();

    // Inspect should now contain a second timestamp from the restarted session.
    const auto& second_time = inspect_data.GetByPath({"root", "session_started_at", "1", "@time"});
    ASSERT_TRUE(second_time.IsNumber());
  }
}

}  // namespace
