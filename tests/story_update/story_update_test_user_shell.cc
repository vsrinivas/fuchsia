// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>

#include <fuchsia/modular/cpp/fidl.h>
#include <fuchsia/ui/views_v1_token/cpp/fidl.h>
#include <lib/app/cpp/connect.h>
#include <lib/app/cpp/startup_context.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/fxl/logging.h>
#include <lib/fxl/macros.h>

#include "peridot/lib/rapidjson/rapidjson.h"
#include "peridot/lib/testing/component_base.h"
#include "peridot/lib/testing/reporting.h"
#include "peridot/lib/testing/testing.h"
#include "peridot/tests/common/defs.h"
#include "peridot/tests/story_update/defs.h"

using modular::testing::TestPoint;

namespace {

// Tests how modules are updated in a story.
class TestApp
    : public modular::testing::ComponentBase<fuchsia::modular::UserShell> {
 public:
  TestApp(fuchsia::sys::StartupContext* const startup_context)
      : ComponentBase(startup_context) {
    TestInit(__FILE__);
  }

  ~TestApp() override = default;

 private:
  TestPoint initialize_{"Initialize()"};
  TestPoint story_create_{"Story Create"};

  // |fuchsia::modular::UserShell|
  void Initialize(fidl::InterfaceHandle<fuchsia::modular::UserShellContext>
                      user_shell_context) override {
    initialize_.Pass();

    user_shell_context_.Bind(std::move(user_shell_context));
    user_shell_context_->GetStoryProvider(story_provider_.NewRequest());

    story_provider_->CreateStory(kCommonNullModule,
                                 [this](fidl::StringPtr story_id) {
                                   story_create_.Pass();
                                   GetController(story_id);
                                 });
  }

  TestPoint root_running_{"Root Module RUNNING"};

  void GetController(fidl::StringPtr story_id) {
    story_provider_->GetController(story_id, story_controller_.NewRequest());

    fidl::InterfaceHandle<fuchsia::ui::views_v1_token::ViewOwner> story_view;
    story_controller_->Start(story_view.NewRequest());

    fidl::VectorPtr<fidl::StringPtr> module_path;
    module_path.push_back("root");

    module0_controller_.events().OnStateChange =
        [this](fuchsia::modular::ModuleState module_state) {
          if (module_state == fuchsia::modular::ModuleState::RUNNING) {
            root_running_.Pass();
            PipelinedAddGetStop();
          }
        };

    story_controller_->GetModuleController(std::move(module_path),
                                           module0_controller_.NewRequest());
  }

  TestPoint module1_stopped_{"Module1 STOPPED"};
  TestPoint module1_gone_{"Module1 gone"};

  void PipelinedAddGetStop() {
    // Tests two invariants:
    //
    // 1. Pipelined fuchsia::modular::AddModule(), GetModuleController(),
    // fuchsia::modular::ModuleController.Stop()
    //    transitions to the module state STOPPED.
    //
    // 2. After fuchsia::modular::ModuleController.Stop() completes (as observed
    // by reaching teh
    //    STOPPED state), GetActiveModules() shows the module as not running.
    //    (This cannot be pipelined because the requests are on different
    //    existing connections.)
    //
    // TODO(mesch): The API as it is defined now does not allow to guarantee to
    // observe a transition through the STARTING and RUNNING states. The
    // implementation also makes no guarantees in the first place to await the
    // module reaching RUNNING before it gets stopped, irrespective of
    // observability of the state transitions.
    //
    // The observability of the STOPPED state, however, is guaranteed.
    fuchsia::modular::Intent intent;
    intent.handler = kCommonNullModule;
    story_controller_->AddModule(nullptr /* parent_module_path */, "module1",
                                 std::move(intent),
                                 nullptr /* surface_relation */);

    fidl::VectorPtr<fidl::StringPtr> module_path;
    module_path.push_back("module1");

    module1_controller_.events().OnStateChange =
        [this](fuchsia::modular::ModuleState new_state) {
          if (new_state == fuchsia::modular::ModuleState::STOPPED) {
            module1_stopped_.Pass();
          }
        };

    story_controller_->GetModuleController(std::move(module_path),
                                           module1_controller_.NewRequest());

    module1_controller_->Stop([this] { GetActiveModules1(); });
  }

  void GetActiveModules1() {
    story_controller_->GetActiveModules(
        nullptr, [this](fidl::VectorPtr<fuchsia::modular::ModuleData> modules) {
          if (modules->size() == 1) {
            module1_gone_.Pass();
          }

          SequentialAddGetStop();
        });
  }

  TestPoint module2_running_{"Module2 RUNNING"};
  TestPoint module2_stopped_{"Module2 STOPPED"};
  TestPoint module2_gone_{"Module2 gone"};

  void SequentialAddGetStop() {
    // Tests these invariants:
    //
    // 1. Pipelined fuchsia::modular::AddModule(), GetModuleController()
    // transitions to the
    //    module state RUNNING.
    //
    // 2. Sequential (sequenced after RUNNING state is reached)
    //    fuchsia::modular::ModuleController.Stop() transitions to the module
    //    state STOPPED.
    //
    // 3. Sequential GetActiveModules() (sequenced after STOPPED state is
    //    reached) shows the module as not running.
    //
    // TODO(mesch): Like above, the API does not make guarantees to be able to
    // observe the STARTING state. It only guarantees to observe the RUNNING
    // state, and only if the module doesn't call Done() in its own.
    //
    // TODO(mesch): If the module calls Done() on its context (as
    // common_done_module, for example, would), it is stopped by the story
    // runner because it's a top level module. If this happens at the same time
    // as this call, the callback may never invoked because it's preempted by
    // the story runner handling the Done() request from the module. Instead,
    // the controller connection is just closed, and flow of control would need
    // to resume from the connection error handler of the module controller.
    fuchsia::modular::Intent intent;
    intent.handler = kCommonNullModule;
    story_controller_->AddModule(nullptr /* parent_module_path */, "module2",
                                 std::move(intent),
                                 nullptr /* surface_relation */);

    fidl::VectorPtr<fidl::StringPtr> module_path;
    module_path.push_back("module2");

    module2_controller_.events().OnStateChange =
        [this](fuchsia::modular::ModuleState module_state) {
          if (module_state == fuchsia::modular::ModuleState::RUNNING) {
            module2_running_.Pass();
            module2_controller_->Stop([this] { GetActiveModules2(); });

          } else if (module_state == fuchsia::modular::ModuleState::STOPPED) {
            module2_stopped_.Pass();
          }
        };

    story_controller_->GetModuleController(std::move(module_path),
                                           module2_controller_.NewRequest());
  }

  void GetActiveModules2() {
    story_controller_->GetActiveModules(
        nullptr, [this](fidl::VectorPtr<fuchsia::modular::ModuleData> modules) {
          if (modules->size() == 1) {
            module2_gone_.Pass();
          }

          Logout();
        });
  }

  void Logout() { user_shell_context_->Logout(); }

  fuchsia::modular::UserShellContextPtr user_shell_context_;
  fuchsia::modular::StoryProviderPtr story_provider_;
  fuchsia::modular::StoryControllerPtr story_controller_;
  fuchsia::modular::StoryInfoPtr story_info_;

  fuchsia::modular::ModuleControllerPtr module0_controller_;
  fuchsia::modular::ModuleControllerPtr module1_controller_;
  fuchsia::modular::ModuleControllerPtr module2_controller_;

  FXL_DISALLOW_COPY_AND_ASSIGN(TestApp);
};

}  // namespace

int main(int /*argc*/, const char** /*argv*/) {
  modular::testing::ComponentMain<TestApp>();
  return 0;
}
