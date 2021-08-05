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
    "inspect_vmo_test_app.cmx";

class InspectTest : public gtest::TestWithEnvironmentFixture {
 protected:
  InspectTest() {
    fuchsia::sys::LaunchInfo launch_info;
    launch_info.url = kTestComponent;

    environment_ = CreateNewEnclosingEnvironment("test", CreateServices());
    environment_->CreateComponent(std::move(launch_info), controller_.NewRequest());
    bool ready = false;
    controller_.events().OnDirectoryReady = [&ready] { ready = true; };
    RunLoopUntil([&ready] { return ready; });
    if (!ready) {
      printf("The output directory is not ready\n");
    }
  }
  ~InspectTest() { CheckShutdown(); }

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

TEST_F(InspectTest, ReadHierarchy) {
  async::Executor executor(dispatcher());
  inspect::contrib::ArchiveReader reader(
      real_services()->Connect<fuchsia::diagnostics::ArchiveAccessor>(),
      {"test/inspect_vmo_test_app.cmx:root"});
  fpromise::result<std::vector<inspect::contrib::DiagnosticsData>, std::string> result;
  executor.schedule_task(
      reader.SnapshotInspectUntilPresent({"inspect_vmo_test_app.cmx"})
          .then([&](fpromise::result<std::vector<inspect::contrib::DiagnosticsData>, std::string>&
                        rest) { result = std::move(rest); }));
  RunLoopUntil([&] { return result.is_ok() || result.is_error(); });

  ASSERT_TRUE(result.is_ok()) << "Error: " << result.error();
  auto inspect_datas = result.take_value();
  ASSERT_EQ(inspect_datas.size(), 1lu);
  const auto& inspect_data = inspect_datas[0];

  const auto& version = inspect_data.GetByPath({"root", "t1", "version"});
  EXPECT_EQ(version, rapidjson::Value("1.0"));

  const auto& frame = inspect_data.GetByPath({"root", "t1", "frame"});
  EXPECT_EQ(frame, rapidjson::Value("b64:AAAA"));

  const auto& value = inspect_data.GetByPath({"root", "t1", "value"});
  EXPECT_EQ(value, rapidjson::Value(-10));

  const auto& active = inspect_data.GetByPath({"root", "t1", "active"});
  EXPECT_EQ(active, rapidjson::Value(true));

  const auto& item0_value = inspect_data.GetByPath({"root", "t1", "item-0x0", "value"});
  EXPECT_EQ(item0_value, rapidjson::Value(10));

  const auto& item1_value = inspect_data.GetByPath({"root", "t1", "item-0x1", "value"});
  EXPECT_EQ(item1_value, rapidjson::Value(100));

  const auto& t2_version = inspect_data.GetByPath({"root", "t2", "version"});
  EXPECT_EQ(t2_version, rapidjson::Value("1.0"));

  const auto& t2_frame = inspect_data.GetByPath({"root", "t2", "frame"});
  EXPECT_EQ(t2_frame, rapidjson::Value("b64:AAAA"));

  const auto& t2_value = inspect_data.GetByPath({"root", "t2", "value"});
  EXPECT_EQ(t2_value, rapidjson::Value(-10));

  const auto& t2_active = inspect_data.GetByPath({"root", "t2", "active"});
  EXPECT_EQ(t2_active, rapidjson::Value(true));

  const auto& item2_value = inspect_data.GetByPath({"root", "t2", "item-0x2", "value"});
  EXPECT_EQ(item2_value, rapidjson::Value(4));
}

}  // namespace
}  // namespace component
