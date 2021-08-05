// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/io/cpp/fidl.h>
#include <lib/async/default.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <lib/inspect/contrib/cpp/archive_reader.h>
#include <lib/sys/cpp/testing/test_with_environment_fixture.h>
#include <zircon/device/vfs.h>

#include <gmock/gmock.h>

#include "src/lib/files/glob.h"
#include "src/lib/fxl/strings/substitute.h"

namespace component {
namespace {

using ::fxl::Substitute;
using sys::testing::EnclosingEnvironment;

constexpr char kTestComponent[] =
    "fuchsia-pkg://fuchsia.com/appmgr_inspect_integration_tests#meta/"
    "inspect_health_test_app.cmx";

class InspectHealthTest : public gtest::TestWithEnvironmentFixture {
 protected:
  InspectHealthTest() {
    fuchsia::sys::LaunchInfo launch_info;
    launch_info.url = kTestComponent;

    environment_ = CreateNewEnclosingEnvironment("test", CreateServices());
    environment_->CreateComponent(std::move(launch_info), controller_.NewRequest());
    bool ready = false;
    controller_.events().OnDirectoryReady = [&ready] { ready = true; };
    RunLoopUntil([&ready] { return ready; });
  }
  ~InspectHealthTest() { CheckShutdown(); }

  void CheckShutdown() {
    controller_->Kill();
    bool done = false;
    controller_.events().OnTerminated = [&done](int64_t code,
                                                fuchsia::sys::TerminationReason reason) {
      ASSERT_EQ(fuchsia::sys::TerminationReason::EXITED, reason);
      done = true;
    };
    RunLoopUntil([&done] { return done; });
  }

 private:
  std::unique_ptr<EnclosingEnvironment> environment_;
  fuchsia::sys::ComponentControllerPtr controller_;
};

TEST_F(InspectHealthTest, ReadHierarchy) {
  async::Executor executor(dispatcher());
  inspect::contrib::ArchiveReader reader(
      real_services()->Connect<fuchsia::diagnostics::ArchiveAccessor>(),
      {"test/inspect_health_test_app.cmx:root"});
  fpromise::result<std::vector<inspect::contrib::DiagnosticsData>, std::string> result;
  executor.schedule_task(
      reader.SnapshotInspectUntilPresent({"inspect_health_test_app.cmx"})
          .then([&](fpromise::result<std::vector<inspect::contrib::DiagnosticsData>, std::string>&
                        rest) { result = std::move(rest); }));
  RunLoopUntil([&] { return result.is_ok() || result.is_error(); });

  ASSERT_TRUE(result.is_ok()) << "Error: " << result.error();
  const auto inspect_datas = result.take_value();
  ASSERT_EQ(inspect_datas.size(), 1lu);
  const auto& inspect_data = inspect_datas[0];

  const auto& status = inspect_data.GetByPath({"root", "fuchsia.inspect.Health", "status"});
  EXPECT_EQ(status, rapidjson::Value("UNHEALTHY"));

  const auto& message = inspect_data.GetByPath({"root", "fuchsia.inspect.Health", "message"});
  EXPECT_EQ(message, rapidjson::Value("Example failure"));

  const auto& timestamp =
      inspect_data.GetByPath({"root", "fuchsia.inspect.Health", "start_timestamp_nanos"});
  EXPECT_TRUE(timestamp.IsNumber());
}

}  // namespace
}  // namespace component
