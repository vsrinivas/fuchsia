// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async/cpp/executor.h>
#include <lib/async/default.h>
#include <lib/inspect/contrib/cpp/archive_reader.h>
#include <lib/sys/cpp/testing/test_with_environment.h>

#include <rapidjson/pointer.h>

#include "gmock/gmock.h"

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

class ArchiveReaderTest : public sys::testing::TestWithEnvironment {
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

using ResultType = fit::result<std::vector<inspect::contrib::DiagnosticsData>, std::string>;

constexpr char cmx1[] = "archive_reader_test_app.cmx";
constexpr char cmx2[] = "archive_reader_test_app_2.cmx";

constexpr char cmx1_selector[] = "test/archive_reader_test_app.cmx:root";
constexpr char cmx2_selector[] = "test/archive_reader_test_app_2.cmx:root";

TEST_F(ArchiveReaderTest, ReadHierarchy) {
  inspect::contrib::ArchiveReader reader(real_services()->Connect<fuchsia::diagnostics::Archive>(),
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

}  // namespace
}  // namespace component
