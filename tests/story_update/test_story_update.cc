// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>

#include "lib/app/cpp/connect.h"
#include "lib/fidl/cpp/bindings/binding.h"
#include "lib/fsl/tasks/message_loop.h"
#include "lib/fxl/command_line.h"
#include "lib/fxl/logging.h"
#include "lib/fxl/macros.h"
#include "lib/fxl/tasks/task_runner.h"
#include "lib/fxl/time/time_delta.h"
#include "lib/module/fidl/module.fidl.h"
#include "lib/ui/views/fidl/view_manager.fidl.h"
#include "lib/user/fidl/user_shell.fidl.h"
#include "peridot/lib/fidl/single_service_app.h"
#include "peridot/lib/rapidjson/rapidjson.h"
#include "peridot/lib/testing/component_base.h"
#include "peridot/lib/testing/reporting.h"
#include "peridot/lib/testing/testing.h"

namespace {

const char kNullModuleUrl[] = "file:///system/apps/modular_tests/null_module";

// A simple module watcher implementation allows to specify the actual
// notification callback as a lambda and update it dynamically.
class ModuleWatcherImpl : modular::ModuleWatcher {
 public:
  ModuleWatcherImpl() : binding_(this) {}
  ~ModuleWatcherImpl() override = default;

  // Registers itself as watcher on the given link. Only one link at a time can
  // be watched.
  void Watch(modular::ModuleControllerPtr* const module) {
    (*module)->Watch(binding_.NewBinding());
  }

  // Sets the function that's called for a notification.
  void Continue(std::function<void(modular::ModuleState)> at) {
    continue_ = at;
  }

 private:
  // |ModuleWatcher|
  void OnStateChange(modular::ModuleState module_state) override {
    FXL_LOG(INFO) << "ModuleWatcher: " << module_state;
    continue_(std::move(module_state));
  }

  std::function<void(modular::ModuleState)> continue_;
  fidl::Binding<modular::ModuleWatcher> binding_;

  FXL_DISALLOW_COPY_AND_ASSIGN(ModuleWatcherImpl);
};

// Tests how modules are updated in a story.
class TestApp : modular::testing::ComponentBase<modular::UserShell> {
 public:
  static void New() {
    new TestApp;  // will delete itself in Terminate().
  }

 private:
  TestApp() { TestInit(__FILE__); }
  ~TestApp() override = default;

  using TestPoint = modular::testing::TestPoint;

  TestPoint initialize_{"Initialize()"};
  TestPoint story_create_{"Story Create"};

  // |UserShell|
  void Initialize(fidl::InterfaceHandle<modular::UserShellContext>
                      user_shell_context) override {
    initialize_.Pass();

    user_shell_context_.Bind(std::move(user_shell_context));
    user_shell_context_->GetStoryProvider(story_provider_.NewRequest());

    story_provider_->CreateStory(kNullModuleUrl,
                                 [this](const fidl::String& story_id) {
                                   story_create_.Pass();
                                   GetController(story_id);
                                 });
  }

  TestPoint root_stop_{"Stop Root Module"};

  void GetController(const fidl::String& story_id) {
    story_provider_->GetController(story_id, story_controller_.NewRequest());

    fidl::InterfaceHandle<mozart::ViewOwner> story_view;
    story_controller_->Start(story_view.NewRequest());

    fidl::Array<fidl::String> module_path;
    module_path.push_back("root");
    story_controller_->GetModuleController(std::move(module_path),
                                           module0_controller_.NewRequest());

    module0_controller_->Stop([this] {
      root_stop_.Pass();
      PipelinedAddGetStop();
    });
  }

  TestPoint module1_starting_{"Module1 STARTING"};
  TestPoint module1_stopped_{"Module1 STOPPED"};
  TestPoint module1_gone_{"Module1 gone"};

  void PipelinedAddGetStop() {
    // Tests two invariants:
    //
    // 1. Pipelined AddModule(), GetModuleController(), then
    //    ModuleController.Stop() transitions through module states STARTING and
    //    STOPPED.
    //
    // 2. After ModuleController.Stop() completes, GetActiveModules() shows the
    //    module as not running. (This cannot be pipelined because the requests
    //    are on different existing connections.)
    story_controller_->AddModule(nullptr, "module1", kNullModuleUrl, "root",
                                 nullptr);

    fidl::Array<fidl::String> module_path;
    module_path.push_back("module1");
    story_controller_->GetModuleController(std::move(module_path),
                                           module1_controller_.NewRequest());

    module1_watcher_.Watch(&module1_controller_);
    module1_watcher_.Continue([this](modular::ModuleState module_state) {
      // Does not pass through RUNNING because we stop it too quick.
      //
      // TODO(mesch): It seems as if the watcher would get connected always
      // before the controller can process any state changes from the context,
      // but there may be a race here so it may get connected only after the
      // context receives Ready() or even Done(). We then would not see STARTING
      // as the initial state of the watcher.
      if (module_state == modular::ModuleState::STARTING) {
        module1_starting_.Pass();
      } else if (module_state == modular::ModuleState::STOPPED) {
        module1_stopped_.Pass();
      }
    });

    module1_controller_->Stop([this] { GetActiveModules1(); });
  }

  void GetActiveModules1() {
    story_controller_->GetActiveModules(
        nullptr, [this](fidl::Array<modular::ModuleDataPtr> modules) {
          if (modules.size() == 0) {
            module1_gone_.Pass();
          }

          SequentialAddGetStop();
        });
  }

  TestPoint module2_starting_{"Module2 STARTING"};
  TestPoint module2_running_{"Module2 RUNNING"};
  TestPoint module2_stopped_{"Module2 STOPPED"};
  TestPoint module2_gone_{"Module2 gone"};

  void SequentialAddGetStop() {
    // Tests two invariants:
    //
    // 1. Sequential AddModule(), GetModuleController(), ModuleController.Stop()
    //    transitions through module states STARTING, RUNNING, and STOPPED.
    //
    // 2. Sequential ModuleController.Stop(), then GetActiveModules() shows the
    //    module as not running.
    story_controller_->AddModule(nullptr, "module2", kNullModuleUrl, "root",
                                 nullptr);

    fidl::Array<fidl::String> module_path;
    module_path.push_back("module2");
    story_controller_->GetModuleController(std::move(module_path),
                                           module2_controller_.NewRequest());

    module2_watcher_.Watch(&module2_controller_);
    module2_watcher_.Continue([this](modular::ModuleState module_state) {
      if (module_state == modular::ModuleState::STARTING) {
        module2_starting_.Pass();

      } else if (module_state == modular::ModuleState::RUNNING) {
        module2_running_.Pass();

        // TODO(mesch): If the module calls Done() on its context (as
        // done_module, for example, would), it is stopped by the story runner
        // because it's a top level module. If this happens at the same time as
        // this call, the callback may never invoked because it's preempted by
        // the story runner handling the Done() request from the module.
        // Instead, the controller connection is just closed.
        module2_controller_->Stop([this] {
          GetActiveModules2();
        });

      } else if (module_state == modular::ModuleState::STOPPED) {
        module2_stopped_.Pass();
      }
    });
  }

  void GetActiveModules2() {
    story_controller_->GetActiveModules(
        nullptr, [this](fidl::Array<modular::ModuleDataPtr> modules) {
          if (modules.size() == 0) {
            module2_gone_.Pass();
          }

          Logout();
        });
  }

  void Logout() { user_shell_context_->Logout(); }

  TestPoint terminate_{"Terminate"};

  // |UserShell|
  void Terminate() override {
    terminate_.Pass();
    DeleteAndQuit();
  }

  modular::UserShellContextPtr user_shell_context_;
  modular::StoryProviderPtr story_provider_;
  modular::StoryControllerPtr story_controller_;
  modular::StoryInfoPtr story_info_;

  modular::ModuleControllerPtr module0_controller_;
  modular::ModuleControllerPtr module1_controller_;
  ModuleWatcherImpl module1_watcher_;
  modular::ModuleControllerPtr module2_controller_;
  ModuleWatcherImpl module2_watcher_;

  FXL_DISALLOW_COPY_AND_ASSIGN(TestApp);
};

}  // namespace

int main(int /*argc*/, const char** /*argv*/) {
  fsl::MessageLoop loop;
  TestApp::New();
  loop.Run();
  return 0;
}
