// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/modular/cpp/fidl.h>
#include <fuchsia/sys/cpp/fidl.h>
#include <lib/async/cpp/task.h>
#include <lib/component/cpp/startup_context.h>
#include <lib/fidl/cpp/binding.h>
#include <src/lib/fxl/command_line.h>
#include <src/lib/fxl/logging.h>
#include <src/lib/fxl/macros.h>

#include "peridot/lib/testing/component_main.h"
#include "peridot/lib/testing/session_shell_base.h"
#include "peridot/public/lib/integration_testing/cpp/reporting.h"
#include "peridot/public/lib/integration_testing/cpp/testing.h"
#include "peridot/tests/common/defs.h"
#include "peridot/tests/embed_shell/defs.h"

using modular::testing::Await;
using modular::testing::Signal;
using modular::testing::TestPoint;

namespace {

// Cf. README.md for what this test does in general and how. The test cases are
// described in detail in comments below.
class TestApp : public modular::testing::SessionShellBase {
 public:
  explicit TestApp(component::StartupContext* const startup_context)
      : SessionShellBase(startup_context) {
    TestInit(__FILE__);

    startup_context->ConnectToEnvironmentService(puppet_master_.NewRequest());

    TestParentChild();
  }

  ~TestApp() override = default;

 private:
  void TestParentChild() {
    puppet_master_->ControlStory(kStoryName, story_puppet_master_.NewRequest());

    fuchsia::modular::AddMod add_mod;
    add_mod.mod_name_transitional = "root";
    add_mod.intent.handler = kParentModuleUrl;
    add_mod.intent.action = kParentModuleAction;
    add_mod.intent.parameters =
        fidl::VectorPtr<fuchsia::modular::IntentParameter>::New(0);

    fuchsia::modular::StoryCommand command;
    command.set_add_mod(std::move(add_mod));

    std::vector<fuchsia::modular::StoryCommand> commands;
    commands.push_back(std::move(command));
    story_puppet_master_->Enqueue(std::move(commands));
    story_puppet_master_->Execute(
        [this](fuchsia::modular::ExecuteResult result) {
          story_provider()->GetController(kStoryName,
                                          story_controller_.NewRequest());
          story_controller_->RequestStart();
        });

    Await(kParentModuleDoneSignal, [this] {
      story_controller_->Stop([this] { TestModuleReinflation(); });
    });
  }

  TestPoint modules_reinflated_correctly_{"Modules re-inflated correctly"};
  // Test that embedded modules are not re-inflated.
  void TestModuleReinflation() {
    story_controller_->RequestStart();
    story_controller_->GetActiveModules(
        [this](std::vector<fuchsia::modular::ModuleData> active_modules) {
          size_t num_embedded_mods = 0u;
          for (const fuchsia::modular::ModuleData& mod : active_modules) {
            num_embedded_mods += mod.is_embedded;
          }
          if (num_embedded_mods == 0 && active_modules.size() == 2u) {
            modules_reinflated_correctly_.Pass();
          }
          Await(kParentModuleDoneSignal, [this] { Logout(); });
        });
  }

  void Logout() { Signal(modular::testing::kTestShutdown); }

  fuchsia::modular::PuppetMasterPtr puppet_master_;
  fuchsia::modular::StoryPuppetMasterPtr story_puppet_master_;
  fuchsia::modular::StoryControllerPtr story_controller_;

  FXL_DISALLOW_COPY_AND_ASSIGN(TestApp);
};

}  // namespace

int main(int argc, const char** argv) {
  auto command_line = fxl::CommandLineFromArgcArgv(argc, argv);
  modular::testing::ComponentMain<TestApp>();
  return 0;
}
