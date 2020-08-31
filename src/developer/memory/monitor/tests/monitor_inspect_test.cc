// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async/cpp/executor.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <lib/inspect/cpp/reader.h>
#include <lib/inspect/service/cpp/reader.h>
#include <lib/inspect/testing/cpp/inspect.h>
#include <lib/sys/cpp/testing/test_with_environment.h>
#include <stdlib.h>
#include <string.h>

#include <fs/vnode.h>
#include <gmock/gmock.h>
#include <src/lib/files/file.h>
#include <src/lib/files/glob.h>

#include "src/lib/fxl/strings/substitute.h"

namespace component {
namespace {

using ::fxl::Substitute;
using sys::testing::EnclosingEnvironment;
using ::testing::ElementsAre;
using ::testing::IsEmpty;
using ::testing::Not;
using ::testing::UnorderedElementsAre;
using namespace inspect::testing;

constexpr char kTestComponent[] =
    "fuchsia-pkg://fuchsia.com/memory_monitor_inspect_integration_tests#meta/"
    "memory_monitor_test_app.cmx";
constexpr char kTestProcessName[] = "memory_monitor_test_app.cmx";

class InspectTest : public sys::testing::TestWithEnvironment {
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
    if (!ready) {
      printf("The output directory is not ready\n");
    }
  }

  void CheckShutdown() {
    controller_->Kill();
    sys::testing::TerminationResult result;
    ASSERT_TRUE(RunComponentUntilTerminated(std::move(controller_), &result));
    ASSERT_EQ(fuchsia::sys::TerminationReason::EXITED, result.reason);
  }

  // Open the root object connection on the given sync pointer.
  // Returns ZX_OK on success.
  fit::result<inspect::Hierarchy> GetInspectHierarchy() {
    files::Glob glob(Substitute("/hub/r/$1/*/c/$0/*/out/diagnostics/fuchsia.inspect.Tree",
                                kTestProcessName, std::string_view(test_case_)));
    if (glob.size() == 0) {
      return fit::error();
    }
    auto path = std::string(*glob.begin());

    fuchsia::inspect::TreePtr tree_ptr;
    zx_status_t status;
    status = fdio_service_connect(path.c_str(), tree_ptr.NewRequest().TakeChannel().release());
    if (status != ZX_OK) {
      return fit::error();
    }

    EXPECT_TRUE(tree_ptr.is_bound());

    async::Executor executor(dispatcher());
    fit::result<inspect::Hierarchy> ret;
    executor.schedule_task(
        inspect::ReadFromTree(std::move(tree_ptr)).then([&](fit::result<inspect::Hierarchy>& res) {
          ret = std::move(res);
        }));
    RunLoopUntil([&] { return ret.is_ok() || ret.is_error(); });
    return ret;
  }

 private:
  std::unique_ptr<EnclosingEnvironment> environment_;
  fuchsia::sys::ComponentControllerPtr controller_;
  const char* test_case_;
};

TEST_F(InspectTest, FirstLaunch) {
  auto result = GetInspectHierarchy();
  ASSERT_TRUE(result.is_ok());
  auto hierarchy = result.take_value();
  EXPECT_THAT(hierarchy,
              AllOf(NodeMatches(AllOf(NameMatches("root"),
                                      PropertyList(UnorderedElementsAre(
                                          StringIs("current", Not(IsEmpty())),
                                          StringIs("current_digest", Not(IsEmpty())),
                                          StringIs("high_water", Not(IsEmpty())),
                                          StringIs("high_water_digest", Not(IsEmpty())))))),

                    ChildrenMatch(UnorderedElementsAre(
                        NodeMatches(AllOf(NameMatches("values"), PropertyList(Not(IsEmpty()))))))));
}

TEST_F(InspectTest, SecondLaunch) {
  // Make sure that the high_water_previous_boot property is made visible only upon the second
  // run.
  auto result = GetInspectHierarchy();
  ASSERT_TRUE(result.is_ok());
  auto hierarchy = result.take_value();
  EXPECT_THAT(hierarchy,
              AllOf(NodeMatches(AllOf(NameMatches("root"),
                                      PropertyList(UnorderedElementsAre(
                                          StringIs("current", Not(IsEmpty())),
                                          StringIs("current_digest", Not(IsEmpty())),
                                          StringIs("high_water", Not(IsEmpty())),
                                          StringIs("high_water_digest", Not(IsEmpty())))))),

                    ChildrenMatch(UnorderedElementsAre(
                        NodeMatches(AllOf(NameMatches("values"), PropertyList(Not(IsEmpty()))))))));
  CheckShutdown();
  Connect();
  result = GetInspectHierarchy();
  ASSERT_TRUE(result.is_ok());
  hierarchy = result.take_value();
  EXPECT_THAT(
      hierarchy,
      AllOf(NodeMatches(AllOf(
                NameMatches("root"),
                PropertyList(UnorderedElementsAre(
                    StringIs("current", Not(IsEmpty())), StringIs("current_digest", Not(IsEmpty())),
                    StringIs("high_water_previous_boot", Not(IsEmpty())),
                    StringIs("high_water_digest_previous_boot", Not(IsEmpty())),
                    StringIs("high_water", Not(IsEmpty())),
                    StringIs("high_water_digest", Not(IsEmpty())))))),
            ChildrenMatch(UnorderedElementsAre(
                NodeMatches(AllOf(NameMatches("values"), PropertyList(Not(IsEmpty()))))))));
}

}  // namespace
}  // namespace component
