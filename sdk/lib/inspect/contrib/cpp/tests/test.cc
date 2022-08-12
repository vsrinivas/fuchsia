// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/async/cpp/executor.h>
#include <lib/async/default.h>
#include <lib/inspect/contrib/cpp/archive_reader.h>
#include <lib/sys/component/cpp/testing/realm_builder.h>
#include <lib/sys/cpp/component_context.h>

#include <memory>

#include <rapidjson/pointer.h>
#include <re2/re2.h>

#include "fuchsia/diagnostics/cpp/fidl.h"
#include "lib/sys/cpp/service_directory.h"
#include "rapidjson/document.h"
#include "src/lib/testing/loop_fixture/real_loop_fixture.h"

namespace component {
namespace {

using namespace component_testing;

std::string regex_replace(const std::string& input, re2::RE2& reg, const std::string& rewrite) {
  std::string output = input;
  re2::RE2::GlobalReplace(&output, reg, rewrite);
  return output;
}

const char CM1_EXPECTED_DATA[] = R"JSON({
    "data_source": "Inspect",
    "metadata": {
        "component_url": "REALM_BUILDER_URL_PREFIX/test_app",
        "filename": "fuchsia.inspect.Tree",
        "timestamp": TIMESTAMP
    },
    "moniker": "REALM_BUILDER_MONIKER_PREFIX/test_app",
    "payload": {
        "root": {
            "option_a": {
                "value": 10
            },
            "option_b": {
                "value": 20
            },
            "version": "v1"
        }
    },
    "version": 1
})JSON";
const char CM2_EXPECTED_DATA[] = R"JSON({
    "data_source": "Inspect",
    "metadata": {
        "component_url": "REALM_BUILDER_URL_PREFIX/test_app_2",
        "filename": "fuchsia.inspect.Tree",
        "timestamp": TIMESTAMP
    },
    "moniker": "REALM_BUILDER_MONIKER_PREFIX/test_app_2",
    "payload": {
        "root": {
            "option_a": {
                "value": 10
            },
            "option_b": {
                "value": 20
            },
            "version": "v1"
        }
    },
    "version": 1
})JSON";

class ArchiveReaderTest : public gtest::RealLoopFixture {
 protected:
  ArchiveReaderTest() : executor_(dispatcher()) {}

  void SetUp() override {
    component_context_ = sys::ComponentContext::Create();

    auto builder = RealmBuilder::Create();

    builder.AddChild("test_app", "#meta/archive_reader_test_app.cm",
                     ChildOptions{.startup_mode = StartupMode::EAGER});
    builder.AddChild("test_app_2", "#meta/archive_reader_test_app.cm",
                     ChildOptions{.startup_mode = StartupMode::EAGER});

    builder.AddRoute(Route{.capabilities = {Protocol{"fuchsia.logger.LogSink"}},
                           .source = ParentRef(),
                           .targets = {ChildRef{"test_app"}, ChildRef{"test_app_2"}}});

    realm_ = std::make_unique<RealmRoot>(builder.Build(dispatcher()));
  }

  void TearDown() override { realm_.reset(); }

  async::Executor& executor() { return executor_; }

  std::shared_ptr<sys::ServiceDirectory> svc() { return component_context_->svc(); }

  std::string cm1_selector() {
    return "realm_builder\\:" + realm_->GetChildName() + "/test_app:root";
  }

  std::string cm2_selector() {
    return "realm_builder\\:" + realm_->GetChildName() + "/test_app_2:root";
  }

 private:
  std::unique_ptr<RealmRoot> realm_;
  async::Executor executor_;
  std::unique_ptr<sys::ComponentContext> component_context_;
};

using ResultType = fpromise::result<std::vector<inspect::contrib::DiagnosticsData>, std::string>;

constexpr char cm1[] = "test_app";
constexpr char cm2[] = "test_app_2";

TEST_F(ArchiveReaderTest, ReadHierarchy) {
  std::cerr << "RUNNING TEST" << std::endl;
  inspect::contrib::ArchiveReader reader(svc()->Connect<fuchsia::diagnostics::ArchiveAccessor>(),
                                         {cm1_selector(), cm2_selector()});

  ResultType result;
  executor().schedule_task(
      reader.SnapshotInspectUntilPresent({"test_app", "test_app_2"}).then([&](ResultType& r) {
        result = std::move(r);
      }));
  RunLoopUntil([&] { return !!result; });

  ASSERT_TRUE(result.is_ok()) << "Error: " << result.error();

  auto value = result.take_value();
  std::sort(value.begin(), value.end(),
            [](auto& a, auto& b) { return a.component_name() < b.component_name(); });

  EXPECT_EQ(cm1, value[0].component_name());
  EXPECT_EQ(cm2, value[1].component_name());

  EXPECT_STREQ("v1", value[0].content()["root"]["version"].GetString());
  EXPECT_STREQ("v1", value[1].content()["root"]["version"].GetString());
}

TEST_F(ArchiveReaderTest, ReadHierarchyWithAlternativeDispatcher) {
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  fuchsia::diagnostics::ArchiveAccessorPtr archive;
  svc()->Connect(archive.NewRequest(loop.dispatcher()));
  async::Executor local_executor(loop.dispatcher());
  inspect::contrib::ArchiveReader reader(std::move(archive), {cm1_selector(), cm2_selector()});

  ResultType result;
  local_executor.schedule_task(
      reader.SnapshotInspectUntilPresent({"test_app", "test_app_2"}).then([&](ResultType& r) {
        result = std::move(r);
      }));

  // Use the alternate loop.
  while (true) {
    loop.Run(zx::deadline_after(zx::msec(10)));
    if (!!result) {
      break;
    }
  }

  ASSERT_TRUE(result.is_ok()) << "Error: " << result.error();

  auto value = result.take_value();
  std::sort(value.begin(), value.end(),
            [](auto& a, auto& b) { return a.component_name() < b.component_name(); });

  EXPECT_EQ(cm1, value[0].component_name());
  EXPECT_EQ(cm2, value[1].component_name());

  EXPECT_STREQ("v1", value[0].content()["root"]["version"].GetString());
  EXPECT_STREQ("v1", value[1].content()["root"]["version"].GetString());
}

TEST_F(ArchiveReaderTest, Sort) {
  inspect::contrib::ArchiveReader reader(svc()->Connect<fuchsia::diagnostics::ArchiveAccessor>(),
                                         {cm1_selector(), cm2_selector()});

  ResultType result;
  executor().schedule_task(
      reader.SnapshotInspectUntilPresent({"test_app", "test_app_2"}).then([&](ResultType& r) {
        result = std::move(r);
      }));
  RunLoopUntil([&] { return !!result; });

  ASSERT_TRUE(result.is_ok()) << "Error: " << result.error();

  auto value = result.take_value();
  ASSERT_EQ(2lu, value.size());

  std::sort(value.begin(), value.end(),
            [](auto& a, auto& b) { return a.component_name() < b.component_name(); });
  value[0].Sort();
  value[1].Sort();

  re2::RE2 timestamp_reg("\"timestamp\": \\d+");
  re2::RE2 url_reg("\"component_url\": \"realm-builder://\\d+/");
  re2::RE2 moniker_reg("\"moniker\": \"realm_builder.+/");

  auto cm1_expected_json = value[0].PrettyJson();
  cm1_expected_json = regex_replace(cm1_expected_json, timestamp_reg, "\"timestamp\": TIMESTAMP");
  cm1_expected_json =
      regex_replace(cm1_expected_json, url_reg, "\"component_url\": \"REALM_BUILDER_URL_PREFIX/");
  cm1_expected_json =
      regex_replace(cm1_expected_json, moniker_reg, "\"moniker\": \"REALM_BUILDER_MONIKER_PREFIX/");

  auto cm2_expected_json = value[1].PrettyJson();
  cm2_expected_json = regex_replace(cm2_expected_json, timestamp_reg, "\"timestamp\": TIMESTAMP");
  cm2_expected_json =
      regex_replace(cm2_expected_json, url_reg, "\"component_url\": \"REALM_BUILDER_URL_PREFIX/");
  cm2_expected_json =
      regex_replace(cm2_expected_json, moniker_reg, "\"moniker\": \"REALM_BUILDER_MONIKER_PREFIX/");

  EXPECT_EQ(CM1_EXPECTED_DATA, cm1_expected_json);
  EXPECT_EQ(CM2_EXPECTED_DATA, cm2_expected_json);
}

}  // namespace
}  // namespace component
