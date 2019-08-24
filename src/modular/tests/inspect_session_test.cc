// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/modular/testing/cpp/fidl.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <lib/fsl/vmo/strings.h>
#include <lib/inspect/testing/cpp/inspect.h>
#include <zircon/device/vfs.h>

#include <peridot/lib/modular_config/modular_config_constants.h>
#include <sdk/lib/inspect/testing/cpp/inspect.h>
#include <src/lib/fxl/strings/substitute.h>

#include "gmock/gmock.h"
#include "src/lib/files/glob.h"
#include "src/modular/lib/modular_test_harness/cpp/fake_session_shell.h"
#include "src/modular/lib/modular_test_harness/cpp/test_harness_fixture.h"

using fuchsia::modular::AddMod;
using fuchsia::modular::StoryCommand;
using fuchsia::modular::StoryInfo;
using fuchsia::modular::StoryInfo2;
using fuchsia::modular::StoryState;
using fuchsia::modular::StoryVisibilityState;
using fuchsia::modular::ViewIdentifier;
using ::fxl::Substitute;
using testing::IsNull;
using testing::Not;

constexpr char kFakeModuleUrl[] = "fuchsia-pkg://example.com/FAKE_MODULE_PKG/fake_module.cmx";
constexpr char kSessionmgrInspectRootGlobPath[] =
    "/hub/r/mth_*_inspect/*/c/sessionmgr.cmx/*/out/objects/root.inspect";
namespace {

class InspectSessionTest : public modular::testing::TestHarnessFixture {
 protected:
  void RunHarnessAndInterceptSessionShell() {
    fuchsia::modular::testing::TestHarnessSpec spec;
    spec.set_environment_suffix("inspect");
    modular_testing::TestHarnessBuilder builder(std::move(spec));
    builder.InterceptSessionShell(fake_session_shell_.GetOnCreateHandler(),
                                  {.sandbox_services = {"fuchsia.modular.SessionShellContext",
                                                        "fuchsia.modular.PuppetMaster"}});
    builder.BuildAndRun(test_harness());

    // Wait for our session shell to start.
    RunLoopUntil([&] { return fake_session_shell_.is_running(); });
  }

  zx_status_t GetInspectVmo(zx::vmo* out_vmo) {
    files::Glob glob(kSessionmgrInspectRootGlobPath);
    if (glob.size() == 0) {
      return ZX_ERR_NOT_FOUND;
    }

    fuchsia::io::FileSyncPtr file;
    zx_status_t status;
    status = fdio_open(std::string(*glob.begin()).c_str(), ZX_FS_RIGHT_READABLE,
                       file.NewRequest().TakeChannel().release());
    if (status != ZX_OK) {
      return status;
    }

    EXPECT_TRUE(file.is_bound());

    fuchsia::io::NodeInfo info;
    auto get_status = file->Describe(&info);
    if (get_status != ZX_OK) {
      printf("get failed\n");
      return get_status;
    }

    if (!info.is_vmofile()) {
      printf("not a vmofile");
      return ZX_ERR_NOT_FOUND;
    }

    *out_vmo = std::move(info.vmofile().vmo);
    return ZX_OK;
  }

  modular::testing::FakeSessionShell fake_session_shell_;
};

TEST_F(InspectSessionTest, NodeHierarchyNoStories) {
  RunHarnessAndInterceptSessionShell();

  fuchsia::modular::StoryProvider* story_provider = fake_session_shell_.story_provider();
  ASSERT_TRUE(story_provider != nullptr);

  bool called_get_stories = false;
  story_provider->GetStories2(
      nullptr, [&called_get_stories](const std::vector<fuchsia::modular::StoryInfo2>& stories) {
        EXPECT_THAT(stories, testing::IsEmpty());
        called_get_stories = true;
      });

  RunLoopUntil([&] { return called_get_stories; });

  // Check the iquery node hierarchy is properly set up with only a root.
  zx::vmo vmo;
  ASSERT_EQ(ZX_OK, GetInspectVmo(&vmo));
  auto hierarchy = inspect::ReadFromVmo(std::move(vmo)).take_value();

  EXPECT_THAT(hierarchy,
              AllOf(inspect::testing::NodeMatches(inspect::testing::NameMatches("root"))));
}

TEST_F(InspectSessionTest, CheckNodeHierarchyStartAndStopStory) {
  RunHarnessAndInterceptSessionShell();

  // Create a new story using PuppetMaster and launch a new story shell,
  // including a mod with extra info.
  fuchsia::modular::PuppetMasterPtr puppet_master;
  fuchsia::modular::StoryPuppetMasterPtr story_master;

  fuchsia::modular::testing::ModularService svc;
  svc.set_puppet_master(puppet_master.NewRequest());
  test_harness()->ConnectToModularService(std::move(svc));

  fuchsia::modular::StoryProvider* story_provider = fake_session_shell_.story_provider();
  ASSERT_TRUE(story_provider != nullptr);
  const char kStoryId[] = "my_story";

  puppet_master->ControlStory(kStoryId, story_master.NewRequest());

  AddMod add_mod;
  add_mod.mod_name_transitional = "mod1";
  add_mod.intent.handler = kFakeModuleUrl;

  StoryCommand command;
  command.set_add_mod(std::move(add_mod));

  std::vector<StoryCommand> commands;
  commands.push_back(std::move(command));

  story_master->Enqueue(std::move(commands));
  bool execute_called = false;
  story_master->Execute(
      [&execute_called](fuchsia::modular::ExecuteResult result) { execute_called = true; });
  RunLoopUntil([&] { return execute_called; });

  zx::vmo vmo_inspect;
  ASSERT_EQ(ZX_OK, GetInspectVmo(&vmo_inspect));
  auto hierarchy = inspect::ReadFromVmo(std::move(vmo_inspect)).take_value();

  // Check that the story_id is in the hierarchy.
  EXPECT_THAT(hierarchy, AllOf(inspect::testing::NodeMatches(inspect::testing::NameMatches("root")),
                               inspect::testing::ChildrenMatch(
                                   UnorderedElementsAre(AllOf(inspect::testing::NodeMatches(
                                       AllOf(inspect::testing::NameMatches("my_story"))))))));

  bool story_deleted = false;
  puppet_master->DeleteStory(kStoryId, [&] { story_deleted = true; });

  RunLoopUntil([&] { return story_deleted; });

  // Check that a node is removed from the hierarchy when a story is removed.
  ASSERT_EQ(ZX_OK, GetInspectVmo(&vmo_inspect));
  hierarchy = inspect::ReadFromVmo(std::move(vmo_inspect)).take_value();
  EXPECT_THAT(hierarchy,
              AllOf(inspect::testing::NodeMatches(inspect::testing::NameMatches("root"))));
}
}  // namespace
