// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/testing/modular/cpp/fidl.h>
#include <lib/inspect/contrib/cpp/archive_reader.h>
#include <lib/modular/testing/cpp/fake_component.h>
#include <lib/stdcompat/optional.h>

#include <gmock/gmock.h>
#include <sdk/lib/modular/testing/cpp/fake_agent.h>

#include "src/lib/fsl/vmo/strings.h"
#include "src/modular/lib/fidl/clone.h"
#include "src/modular/lib/modular_config/modular_config.h"
#include "src/modular/lib/modular_config/modular_config_constants.h"
#include "src/modular/lib/modular_test_harness/cpp/fake_session_shell.h"
#include "src/modular/lib/modular_test_harness/cpp/test_harness_fixture.h"
#include "src/modular/lib/pseudo_dir/pseudo_dir_server.h"

namespace {

using ::testing::HasSubstr;

constexpr char kBasemgrSelector[] = "*_inspect/basemgr.cmx:root";
constexpr char kBasemgrComponentName[] = "basemgr.cmx";

class BasemgrTest : public modular_testing::TestHarnessFixture {
 public:
  BasemgrTest() : executor_(dispatcher()) {}

  fpromise::result<inspect::contrib::DiagnosticsData> GetInspectDiagnosticsData() {
    auto archive = real_services()->Connect<fuchsia::diagnostics::ArchiveAccessor>();

    inspect::contrib::ArchiveReader reader(std::move(archive), {kBasemgrSelector});
    fpromise::result<std::vector<inspect::contrib::DiagnosticsData>, std::string> result;
    executor_.schedule_task(
        reader.SnapshotInspectUntilPresent({kBasemgrComponentName})
            .then([&](fpromise::result<std::vector<inspect::contrib::DiagnosticsData>, std::string>&
                          snapshot_result) { result = std::move(snapshot_result); }));
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

  async::Executor executor_;
};

// Tests that when multiple session shell are provided the first is picked
TEST_F(BasemgrTest, StartFirstShellWhenMultiple) {
  fuchsia::modular::testing::TestHarnessSpec spec;
  modular_testing::TestHarnessBuilder builder(std::move(spec));

  // Session shells used in list
  auto session_shell = modular_testing::FakeSessionShell::CreateWithDefaultOptions();
  auto session_shell2 = modular_testing::FakeSessionShell::CreateWithDefaultOptions();

  // Create session shell list (appended in order)
  builder.InterceptSessionShell(session_shell->BuildInterceptOptions());
  builder.InterceptSessionShell(session_shell2->BuildInterceptOptions());
  builder.BuildAndRun(test_harness());

  // Run until one is started
  RunLoopUntil([&] { return session_shell->is_running() || session_shell2->is_running(); });

  // Assert only first one is started
  EXPECT_TRUE(session_shell->is_running());
  EXPECT_FALSE(session_shell2->is_running());
}

// Tests that basemgr exposes its configuration in Inspect.
TEST_F(BasemgrTest, ExposesConfigInInspect) {
  auto session_shell = modular_testing::FakeSessionShell::CreateWithDefaultOptions();

  fuchsia::modular::testing::TestHarnessSpec spec;
  spec.set_environment_suffix("inspect");

  modular_testing::TestHarnessBuilder builder(std::move(spec));
  builder.InterceptSessionShell(session_shell->BuildInterceptOptions());
  builder.BuildAndRun(test_harness());

  RunLoopUntil([&] { return session_shell->is_running(); });

  auto inspect_result = GetInspectDiagnosticsData();
  ASSERT_TRUE(inspect_result.is_ok());
  auto inspect_data = inspect_result.take_value();

  // The inspect property should contain configuration that uses |session_shell|.
  const auto& config_value = inspect_data.GetByPath({"root", modular_config::kInspectConfig});
  ASSERT_TRUE(config_value.IsString());
  EXPECT_THAT(config_value.GetString(), HasSubstr(session_shell->url()));
}

}  // namespace
