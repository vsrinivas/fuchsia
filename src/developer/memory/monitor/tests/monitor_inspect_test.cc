// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async/cpp/executor.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <lib/inspect/contrib/cpp/archive_reader.h>
#include <lib/sys/cpp/testing/test_with_environment_fixture.h>
#include <stdlib.h>
#include <string.h>

#include <sstream>

#include <gmock/gmock.h>
#include <src/lib/files/file.h>
#include <src/lib/files/glob.h>

#include "src/lib/fxl/strings/substitute.h"
#include "src/lib/storage/vfs/cpp/vnode.h"

namespace component {
namespace {

using ::fxl::Substitute;
using inspect::contrib::DiagnosticsData;
using sys::testing::EnclosingEnvironment;

constexpr char kTestComponent[] =
    "fuchsia-pkg://fuchsia.com/memory_monitor_inspect_integration_tests#meta/"
    "memory_monitor_test_app.cmx";
constexpr char kTestProcessName[] = "memory_monitor_test_app.cmx";

class InspectTest : public gtest::TestWithEnvironmentFixture {
 protected:
  InspectTest() : test_case_(::testing::UnitTest::GetInstance()->current_test_info()->name()) {
    auto env_services = sys::ServiceDirectory::CreateFromNamespace();
    fuchsia::sys::EnvironmentPtr parent_env;
    env_services->Connect(parent_env.NewRequest());
    auto services = sys::testing::EnvironmentServices::Create(parent_env);
    services->AllowParentService("fuchsia.kernel.Stats");
    environment_ = CreateNewEnclosingEnvironment(test_case_, std::move(services));
    Connect();
  }

  ~InspectTest() { CheckShutdown(); }

  void Connect() {
    fuchsia::sys::LaunchInfo launch_info;
    launch_info.url = kTestComponent;

    environment_->CreateComponent(std::move(launch_info), controller_.NewRequest());
    bool ready = false;
    controller_.events().OnDirectoryReady = [&ready] { ready = true; };
    RunLoopUntil([&ready] { return ready; });
  }

  void CheckShutdown() {
    controller_->Kill();
    sys::testing::TerminationResult result;
    ASSERT_TRUE(RunComponentUntilTerminated(std::move(controller_), &result));
    ASSERT_EQ(fuchsia::sys::TerminationReason::EXITED, result.reason);
  }

  // Open the root object connection on the given sync pointer.
  // Returns ZX_OK on success.
  fpromise::result<DiagnosticsData> GetInspect() {
    auto archive = real_services()->Connect<fuchsia::diagnostics::ArchiveAccessor>();
    std::stringstream selector;
    selector << test_case_ << "/" << kTestProcessName << ":root";
    inspect::contrib::ArchiveReader reader(std::move(archive), {selector.str()});
    fpromise::result<std::vector<DiagnosticsData>, std::string> result;
    async::Executor executor(dispatcher());
    executor.schedule_task(
        reader.SnapshotInspectUntilPresent({kTestProcessName})
            .then([&](fpromise::result<std::vector<DiagnosticsData>, std::string>& rest) {
              result = std::move(rest);
            }));
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

 private:
  std::unique_ptr<EnclosingEnvironment> environment_;
  fuchsia::sys::ComponentControllerPtr controller_;
  const char* test_case_;
};

void expect_string_not_empty(const DiagnosticsData& data, const std::vector<std::string>& path) {
  auto& value = data.GetByPath(path);
  EXPECT_EQ(value.GetType(), rapidjson::kStringType) << path.back() << " is not a string";
  EXPECT_NE(value.GetStringLength(), 0u) << path.back() << " is empty";
}

void expect_object_not_empty(const DiagnosticsData& data, const std::vector<std::string>& path) {
  auto& value = data.GetByPath(path);
  EXPECT_EQ(value.GetType(), rapidjson::kObjectType) << path.back() << " is not an object";
  EXPECT_FALSE(value.ObjectEmpty()) << path.back() << " is empty";
}

TEST_F(InspectTest, FirstLaunch) {
  auto result = GetInspect();
  ASSERT_TRUE(result.is_ok());
  auto data = result.take_value();
  expect_string_not_empty(data, {"root", "current"});
  expect_string_not_empty(data, {"root", "current_digest"});
  expect_string_not_empty(data, {"root", "high_water"});
  expect_string_not_empty(data, {"root", "high_water_digest"});
  expect_object_not_empty(data, {"root", "values"});
}

TEST_F(InspectTest, SecondLaunch) {
  // Make sure that the *_previous_boot properties are made visible only upon
  // the second run.
  auto result = GetInspect();
  ASSERT_TRUE(result.is_ok());
  auto data = result.take_value();
  expect_string_not_empty(data, {"root", "current"});
  expect_string_not_empty(data, {"root", "current_digest"});
  expect_string_not_empty(data, {"root", "high_water"});
  expect_string_not_empty(data, {"root", "high_water_digest"});
  expect_object_not_empty(data, {"root", "values"});

  CheckShutdown();
  Connect();

  result = GetInspect();
  ASSERT_TRUE(result.is_ok());
  data = result.take_value();
  expect_string_not_empty(data, {"root", "current"});
  expect_string_not_empty(data, {"root", "current_digest"});
  expect_string_not_empty(data, {"root", "high_water"});
  expect_string_not_empty(data, {"root", "high_water_previous_boot"});
  expect_string_not_empty(data, {"root", "high_water_digest"});
  expect_string_not_empty(data, {"root", "high_water_digest_previous_boot"});
  expect_object_not_empty(data, {"root", "values"});
}

}  // namespace
}  // namespace component
