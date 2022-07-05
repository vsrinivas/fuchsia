// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/diagnostics/cpp/fidl.h>
#include <lib/inspect/contrib/cpp/archive_reader.h>
#include <lib/sys/component/cpp/testing/realm_builder.h>
#include <lib/sys/cpp/service_directory.h>

#include <optional>
#include <string>

#include <rapidjson/document.h>
#include <rapidjson/pointer.h>
#include <src/lib/fsl/vmo/strings.h>

#include "src/lib/testing/loop_fixture/real_loop_fixture.h"

using ContentVector = std::vector<fuchsia::diagnostics::FormattedContent>;
using inspect::contrib::DiagnosticsData;

class IntegrationTest : public gtest::RealLoopFixture {
 protected:
  DiagnosticsData GetInspectJson(const std::string& name) {
    fuchsia::diagnostics::ArchiveAccessorPtr archive;
    auto svc = sys::ServiceDirectory::CreateFromNamespace();
    svc->Connect(archive.NewRequest());

    std::stringstream selector;
    selector << "*/" << name << ":root";

    inspect::contrib::ArchiveReader reader(std::move(archive), {selector.str()});
    fpromise::result<std::vector<DiagnosticsData>, std::string> result;
    async::Executor executor(dispatcher());
    executor.schedule_task(reader.SnapshotInspectUntilPresent({name}).then(
        [&](fpromise::result<std::vector<DiagnosticsData>, std::string>& rest) {
          result = std::move(rest);
        }));
    RunLoopUntil([&] { return result.is_ok() || result.is_error(); });

    EXPECT_EQ(result.is_error(), false) << "Error was " << result.error();
    EXPECT_EQ(result.value().size(), 1ul) << "Expected only one component";

    return std::move(result.value()[0]);
  }

  void RunTest(const std::string& url, const std::string& name, bool replace_config_value) {
    auto realm_builder = component_testing::RealmBuilder::Create();
    auto options =
        component_testing::ChildOptions{.startup_mode = component_testing::StartupMode::EAGER};
    realm_builder.AddChild(name, url, options);

    if (replace_config_value) {
      // [START config_replace]
      realm_builder.ReplaceConfigValue(name, "greeting", "Fuchsia");
      // [END config_replace]
    }

    realm_builder.AddRoute(component_testing::Route{
        .capabilities = {component_testing::Protocol{"fuchsia.logger.LogSink"}},
        .source = component_testing::ParentRef(),
        .targets = {component_testing::ChildRef{name}}});
    auto realm = realm_builder.Build();

    auto data = GetInspectJson(name);

    if (replace_config_value) {
      EXPECT_EQ(rapidjson::Value("Fuchsia"), data.GetByPath({"root", "config", "greeting"}));
    } else {
      EXPECT_EQ(rapidjson::Value("World"), data.GetByPath({"root", "config", "greeting"}));
    }
  }
};

TEST_F(IntegrationTest, ConfigCpp) { RunTest("#meta/config_example.cm", "config_example", false); }

TEST_F(IntegrationTest, ConfigCppReplace) {
  RunTest("#meta/config_example.cm", "config_example_replace", true);
}
