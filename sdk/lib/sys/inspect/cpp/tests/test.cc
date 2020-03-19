// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/inspect/cpp/fidl.h>
#include <fuchsia/io/cpp/fidl.h>
#include <lib/async/cpp/executor.h>
#include <lib/async/default.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <lib/inspect/service/cpp/reader.h>
#include <lib/inspect/testing/cpp/inspect.h>
#include <lib/sys/cpp/testing/test_with_environment.h>
#include <zircon/device/vfs.h>

#include <src/lib/fxl/strings/substitute.h>

#include "gmock/gmock.h"
#include "src/lib/files/glob.h"

namespace {

using ::fxl::Substitute;
using ::testing::Contains;
using ::testing::UnorderedElementsAre;
using ::testing::_;
using sys::testing::EnclosingEnvironment;
using namespace inspect::testing;

constexpr char kTestComponent[] =
    "fuchsia-pkg://fuchsia.com/sys_inspect_cpp_tests#meta/"
    "sys_inspect_cpp_bin.cmx";
constexpr char kTestProcessName[] = "sys_inspect_cpp_bin.cmx";

class SysInspectTest : public sys::testing::TestWithEnvironment {
 protected:
  SysInspectTest() : executor_(dispatcher()) {
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
  ~SysInspectTest() { CheckShutdown(); }

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

  // Open the root of the tree service.
  // Returns ZX_OK on success.
  zx_status_t GetInspectTree(fuchsia::inspect::TreePtr* ptr) {
    files::Glob glob(Substitute("/hub/r/test/*/c/$0/*/out/diagnostics/$1", kTestProcessName,
                                std::string(fuchsia::inspect::Tree::Name_))
                         .c_str());
    if (glob.size() == 0) {
      return ZX_ERR_NOT_FOUND;
    }

    zx_status_t status;
    status = fdio_service_connect(std::string(*glob.begin()).c_str(),
                                  ptr->NewRequest().TakeChannel().release());
    return status;
  }

  async::Executor executor_;

 private:
  std::unique_ptr<EnclosingEnvironment> environment_;
  fuchsia::sys::ComponentControllerPtr controller_;
};

TEST_F(SysInspectTest, ReadHierarchy) {
  bool done = false;
  fit::result<inspect::Hierarchy> result;
  fuchsia::inspect::TreePtr ptr;
  ASSERT_EQ(ZX_OK, GetInspectTree(&ptr));
  executor_.schedule_task(
      inspect::ReadFromTree(std::move(ptr)).then([&](fit::result<inspect::Hierarchy>& res) {
        result = std::move(res);
        done = true;
      }));

  RunLoopUntil([&] { return done; });

  EXPECT_THAT(
      result.value(),
      AllOf(NodeMatches(AllOf(NameMatches("root"), PropertyList(UnorderedElementsAre(
                                                       IntIs("val1", 1), IntIs("val2", 2),
                                                       IntIs("val3", 3), IntIs("val4", 4))))),
            ChildrenMatch(Contains(NodeMatches(AllOf(
                NameMatches("child"), PropertyList(UnorderedElementsAre(IntIs("val", 0)))))))));
}

TEST_F(SysInspectTest, ReadHealth) {
  bool done = false;
  fit::result<inspect::Hierarchy> result;
  fuchsia::inspect::TreePtr ptr;
  ASSERT_EQ(ZX_OK, GetInspectTree(&ptr));
  executor_.schedule_task(
      inspect::ReadFromTree(std::move(ptr)).then([&](fit::result<inspect::Hierarchy>& res) {
        result = std::move(res);
        done = true;
      }));

  RunLoopUntil([&] { return done; });

  EXPECT_THAT(result.value(),
              ChildrenMatch(Contains(NodeMatches(
                  AllOf(NameMatches("fuchsia.inspect.Health"),
                        PropertyList(UnorderedElementsAre(
                            StringIs("status", "OK"), IntIs("start_timestamp_nanos", _))))))));
}

}  // namespace
