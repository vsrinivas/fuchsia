// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <set>
#include <utility>

#include <fuchsia/modular/cpp/fidl.h>
#include <fuchsia/sys/cpp/fidl.h>
#include <fuchsia/ui/policy/cpp/fidl.h>
#include <lib/async/cpp/task.h>
#include <lib/async/default.h>
#include <lib/component/cpp/startup_context.h>
#include <lib/fidl/cpp/binding.h>
#include <src/lib/fxl/command_line.h>
#include <src/lib/fxl/logging.h>
#include <src/lib/fxl/macros.h>
#include <src/lib/fxl/time/time_delta.h>

#include "peridot/lib/fidl/clone.h"
#include "peridot/lib/rapidjson/rapidjson.h"
#include "peridot/lib/testing/component_main.h"
#include "peridot/lib/testing/session_shell_base.h"
#include "peridot/public/lib/integration_testing/cpp/reporting.h"
#include "peridot/public/lib/integration_testing/cpp/testing.h"
#include "peridot/tests/common/defs.h"
#include "peridot/tests/story_shell_factory/story_shell_factory_impl.h"
#include "peridot/tests/story_shell_factory/story_shell_impl.h"

using modular::testing::Fail;
using modular::testing::Get;
using modular::testing::Signal;
using modular::testing::TestPoint;

namespace {

// Cf. README.md for what this test does and how.
class TestApp : public modular::testing::SessionShellBase {
 public:
  explicit TestApp(component::StartupContext* const startup_context)
      : SessionShellBase(startup_context) {
    TestInit(__FILE__);

    startup_context->ConnectToEnvironmentService(puppet_master_.NewRequest());

    startup_context->outgoing().AddPublicService(
        story_shell_factory_impl_.GetHandler());

    TestStoryShellAttachDetach();
  }

  ~TestApp() override = default;

 private:
  // TestStoryShellAttachDetach
  //
  // Create a story, which should start running automatically.
  //
  // Verify the story shell is attached through StoryShellFactory.AttachStory()
  //
  // Verify that, when the story is stopped, the story shell is detached via
  // StoryShellFactory.DetachStory()
  TestPoint story_shell_attach_detach_create_{
      "TestStoryShellAttachDetach: Create"};

  void TestStoryShellAttachDetach() {
    puppet_master_->ControlStory("story1", story_puppet_master_.NewRequest());

    fuchsia::modular::AddMod add_mod;
    add_mod.mod_name_transitional = "mod1";
    add_mod.intent.handler = kCommonNullModule;

    fuchsia::modular::StoryCommand command;
    command.set_add_mod(std::move(add_mod));

    std::vector<fuchsia::modular::StoryCommand> commands;
    commands.push_back(std::move(command));

    story_puppet_master_->Enqueue(std::move(commands));

    story_shell_factory_impl_.set_on_attach_story(
        [this](std::string,
               fidl::InterfaceRequest<fuchsia::modular::StoryShell> request) {
          story_shell_attach_detach_attach_story_.Pass();

          story_shell_impl_.GetHandler()(std::move(request));
        });

    story_puppet_master_->Execute(
        [this](fuchsia::modular::ExecuteResult result) {
          story_shell_attach_detach_create_.Pass();
          TestStoryShellAttachDetach_RunStory();
        });
  }

  TestPoint story_shell_attach_detach_state_after_run_{
      "TestStoryShellAttachDetach: State after Run"};
  TestPoint story_shell_attach_detach_attach_story_{
      "TestStoryShellAttachDetach: AttachStory"};

  void TestStoryShellAttachDetach_RunStory() {
    story_provider()->GetController("story1", story_controller_.NewRequest());
    story_controller_->GetInfo([this](fuchsia::modular::StoryInfo info,
                                      fuchsia::modular::StoryState state) {
      if (state == fuchsia::modular::StoryState::RUNNING) {
        story_shell_attach_detach_state_after_run_.Pass();
        TestStoryShellAttachDetach_StopStory();
      }
    });
  }

  TestPoint story_shell_attach_detach_detach_story_{
      "TestStoryShellAttachDetach: DetachStory"};
  TestPoint story_shell_attach_detach_stop_{"TestStoryShellAttachDetach: Stop"};
  TestPoint story_shell_attach_detach_delete_{
      "TestStoryShellAttachDetach: DeleteStory"};

  void TestStoryShellAttachDetach_StopStory() {
    story_shell_factory_impl_.set_on_detach_story(
        [this]() { story_shell_attach_detach_detach_story_.Pass(); });

    story_controller_->Stop([this] {
      TeardownStoryController();
      story_shell_attach_detach_stop_.Pass();

      puppet_master_->DeleteStory(story_info_.id, [this] {
        story_shell_attach_detach_delete_.Pass();

        Signal(modular::testing::kTestShutdown);
      });
    });
  }

  void TeardownStoryController() { story_controller_.Unbind(); }

  fuchsia::modular::PuppetMasterPtr puppet_master_;
  fuchsia::modular::StoryPuppetMasterPtr story_puppet_master_;
  fuchsia::modular::StoryControllerPtr story_controller_;
  modular::testing::StoryShellFactoryImpl story_shell_factory_impl_;
  modular::testing::StoryShellImpl story_shell_impl_;
  fuchsia::modular::StoryInfo story_info_;

  FXL_DISALLOW_COPY_AND_ASSIGN(TestApp);
};

}  // namespace

int main(int /* argc */, const char** /* argv */) {
  modular::testing::ComponentMain<TestApp>();
  return 0;
}
