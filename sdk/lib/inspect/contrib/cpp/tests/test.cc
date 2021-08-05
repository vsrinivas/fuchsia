// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async/cpp/executor.h>
#include <lib/async/default.h>
#include <lib/inspect/contrib/cpp/archive_reader.h>
#include <lib/sys/cpp/testing/test_with_environment_fixture.h>

#include <regex>

#include <gmock/gmock.h>
#include <rapidjson/pointer.h>

#include "lib/zx/time.h"

namespace component {
namespace {

using sys::testing::EnclosingEnvironment;
using ::testing::Eq;
using ::testing::Pointee;

constexpr char kTestComponent1[] =
    "fuchsia-pkg://fuchsia.com/archive_reader_integration_tests#meta/"
    "archive_reader_test_app.cmx";
constexpr char kTestComponent2[] =
    "fuchsia-pkg://fuchsia.com/archive_reader_integration_tests#meta/"
    "archive_reader_test_app_2.cmx";

const char CMX1_EXPECTED_DATA[] = R"JSON({
    "data_source": "Inspect",
    "metadata": {
        "component_url": "fuchsia-pkg://fuchsia.com/archive_reader_integration_tests#meta/archive_reader_test_app.cmx",
        "errors": null,
        "filename": "fuchsia.inspect.Tree",
        "timestamp": TIMESTAMP
    },
    "moniker": "test/archive_reader_test_app.cmx",
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
const char CMX2_EXPECTED_DATA[] = R"JSON({
    "data_source": "Inspect",
    "metadata": {
        "component_url": "fuchsia-pkg://fuchsia.com/archive_reader_integration_tests#meta/archive_reader_test_app_2.cmx",
        "errors": null,
        "filename": "fuchsia.inspect.Tree",
        "timestamp": TIMESTAMP
    },
    "moniker": "test/archive_reader_test_app_2.cmx",
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

class ArchiveReaderTest : public gtest::TestWithEnvironmentFixture {
 protected:
  ArchiveReaderTest() : executor_(dispatcher()) {
    environment_ = CreateNewEnclosingEnvironment("test", CreateServices());

    {
      fuchsia::sys::LaunchInfo launch_info;
      launch_info.url = kTestComponent1;
      bool ready = false;
      environment_->CreateComponent(std::move(launch_info), controller_.NewRequest());
      controller_.events().OnDirectoryReady = [&ready] { ready = true; };
      RunLoopUntil([&ready] { return ready; });
      if (!ready) {
        printf("The output directory is not ready for component 1\n");
      }
      controller_->Detach();
    }

    {
      fuchsia::sys::LaunchInfo launch_info;
      launch_info.url = kTestComponent2;
      bool ready = false;
      environment_->CreateComponent(std::move(launch_info), controller_.NewRequest());
      controller_.events().OnDirectoryReady = [&ready] { ready = true; };
      RunLoopUntil([&ready] { return ready; });
      if (!ready) {
        printf("The output directory is not ready for component 2\n");
      }
      controller_->Detach();
    }
  }

 protected:
  async::Executor executor_;

 private:
  std::unique_ptr<EnclosingEnvironment> environment_;
  fuchsia::sys::ComponentControllerPtr controller_;
};

using ResultType = fpromise::result<std::vector<inspect::contrib::DiagnosticsData>, std::string>;

constexpr char cmx1[] = "archive_reader_test_app.cmx";
constexpr char cmx2[] = "archive_reader_test_app_2.cmx";

constexpr char cmx1_selector[] = "test/archive_reader_test_app.cmx:root";
constexpr char cmx2_selector[] = "test/archive_reader_test_app_2.cmx:root";

TEST_F(ArchiveReaderTest, ReadHierarchy) {
  inspect::contrib::ArchiveReader reader(
      real_services()->Connect<fuchsia::diagnostics::ArchiveAccessor>(),
      {cmx1_selector, cmx2_selector});

  ResultType result;
  executor_.schedule_task(reader
                              .SnapshotInspectUntilPresent(
                                  {"archive_reader_test_app.cmx", "archive_reader_test_app_2.cmx"})
                              .then([&](ResultType& r) { result = std::move(r); }));
  RunLoopUntil([&] { return !!result; });

  ASSERT_TRUE(result.is_ok()) << "Error: " << result.error();

  auto value = result.take_value();
  std::sort(value.begin(), value.end(),
            [](auto& a, auto& b) { return a.component_name() < b.component_name(); });

  EXPECT_EQ(cmx1, value[0].component_name());
  EXPECT_EQ(cmx2, value[1].component_name());

  EXPECT_STREQ("v1", value[0].content()["root"]["version"].GetString());
  EXPECT_STREQ("v1", value[1].content()["root"]["version"].GetString());
}

TEST_F(ArchiveReaderTest, ReadHierarchyWithAlternativeDispatcher) {
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  fuchsia::diagnostics::ArchiveAccessorPtr archive;
  real_services()->Connect(archive.NewRequest(loop.dispatcher()));
  async::Executor local_executor(loop.dispatcher());
  inspect::contrib::ArchiveReader reader(std::move(archive), {cmx1_selector, cmx2_selector});

  ResultType result;
  local_executor.schedule_task(reader
                                   .SnapshotInspectUntilPresent({"archive_reader_test_app.cmx",
                                                                 "archive_reader_test_app_2.cmx"})
                                   .then([&](ResultType& r) { result = std::move(r); }));

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

  EXPECT_EQ(cmx1, value[0].component_name());
  EXPECT_EQ(cmx2, value[1].component_name());

  EXPECT_STREQ("v1", value[0].content()["root"]["version"].GetString());
  EXPECT_STREQ("v1", value[1].content()["root"]["version"].GetString());
}

TEST_F(ArchiveReaderTest, Sort) {
  inspect::contrib::ArchiveReader reader(
      real_services()->Connect<fuchsia::diagnostics::ArchiveAccessor>(),
      {cmx1_selector, cmx2_selector});

  ResultType result;
  executor_.schedule_task(reader
                              .SnapshotInspectUntilPresent(
                                  {"archive_reader_test_app.cmx", "archive_reader_test_app_2.cmx"})
                              .then([&](ResultType& r) { result = std::move(r); }));
  RunLoopUntil([&] { return !!result; });

  ASSERT_TRUE(result.is_ok()) << "Error: " << result.error();

  auto value = result.take_value();
  ASSERT_EQ(2lu, value.size());

  std::sort(value.begin(), value.end(),
            [](auto& a, auto& b) { return a.component_name() < b.component_name(); });
  value[0].Sort();
  value[1].Sort();

  std::regex reg("\"timestamp\": \\d+");
  EXPECT_EQ(CMX1_EXPECTED_DATA,
            regex_replace(value[0].PrettyJson(), reg, "\"timestamp\": TIMESTAMP"));
  EXPECT_EQ(CMX2_EXPECTED_DATA,
            regex_replace(value[1].PrettyJson(), reg, "\"timestamp\": TIMESTAMP"));
}

}  // namespace
}  // namespace component
