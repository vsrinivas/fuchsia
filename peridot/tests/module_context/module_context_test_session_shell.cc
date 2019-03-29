// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>

#include <fuchsia/modular/cpp/fidl.h>
#include <lib/callback/scoped_callback.h>
#include <lib/component/cpp/connect.h>
#include <lib/component/cpp/startup_context.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/fsl/vmo/strings.h>
#include <src/lib/fxl/logging.h>
#include <src/lib/fxl/macros.h>

#include "peridot/lib/rapidjson/rapidjson.h"
#include "peridot/lib/testing/component_main.h"
#include "peridot/lib/testing/session_shell_base.h"
#include "peridot/public/lib/integration_testing/cpp/reporting.h"
#include "peridot/public/lib/integration_testing/cpp/testing.h"
#include "peridot/tests/common/defs.h"
#include "peridot/tests/module_context/defs.h"

using ::modular::testing::Await;
using ::modular::testing::Signal;
using ::modular::testing::TestPoint;

namespace {

const char kStoryName[] = "story";

// A simple story activity watcher implementation.
class StoryActivityWatcherImpl : fuchsia::modular::StoryActivityWatcher {
 public:
  StoryActivityWatcherImpl()
      : binding_(this),
        on_notify_([](std::string,
                      std::vector<fuchsia::modular::OngoingActivityType>) {}) {}
  ~StoryActivityWatcherImpl() override = default;

  void Watch(fuchsia::modular::StoryProvider* const story_provider) {
    story_provider->WatchActivity(binding_.NewBinding());
  }

  void OnNotify(
      fit::function<void(std::string,
                         std::vector<fuchsia::modular::OngoingActivityType>)>
          on_notify) {
    on_notify_ = std::move(on_notify);
  }

 private:
  // |fuchsia::modular::StoryActivityWatcher|
  void OnStoryActivityChange(
      std::string story_id,
      std::vector<fuchsia::modular::OngoingActivityType> activities) override {
    on_notify_(std::move(story_id), std::move(activities));
  }

  fidl::Binding<fuchsia::modular::StoryActivityWatcher> binding_;
  fit::function<void(std::string,
                     std::vector<fuchsia::modular::OngoingActivityType>)>
      on_notify_;

  FXL_DISALLOW_COPY_AND_ASSIGN(StoryActivityWatcherImpl);
};

class TestApp : public modular::testing::SessionShellBase {
 public:
  TestApp(component::StartupContext* const startup_context)
      : SessionShellBase(startup_context), weak_ptr_factory_(this) {
    TestInit(__FILE__);

    startup_context->ConnectToEnvironmentService(puppet_master_.NewRequest());

    CreateStory();
  }

  ~TestApp() override = default;

 private:
  TestPoint story_create_{"Created story."};
  void CreateStory() {
    std::vector<fuchsia::modular::StoryCommand> commands;
    {
      fuchsia::modular::AddMod add_mod;
      add_mod.mod_name_transitional = kFirstModuleName;
      add_mod.intent = IntentWithParameterString(kFirstModuleName);

      fuchsia::modular::StoryCommand command;
      command.set_add_mod(std::move(add_mod));
      commands.push_back(std::move(command));
    }
    {
      fuchsia::modular::AddMod add_mod;
      add_mod.mod_name_transitional = kSecondModuleName;
      add_mod.intent = IntentWithParameterString(kSecondModuleName);

      fuchsia::modular::StoryCommand command;
      command.set_add_mod(std::move(add_mod));
      commands.push_back(std::move(command));
    }
    {
      fuchsia::modular::Intent intent;
      intent.handler = kEntityModuleUrl;
      intent.action = kEntityIntentAction;
      fuchsia::modular::AddMod add_mod;
      add_mod.mod_name_transitional = "entity_module";
      add_mod.intent = std::move(intent);
      add_mod.surface_parent_mod_name.resize(0);

      fuchsia::modular::StoryCommand command;
      command.set_add_mod(std::move(add_mod));
      commands.push_back(std::move(command));
    }

    puppet_master_->ControlStory(kStoryName, story_puppet_master_.NewRequest());
    story_puppet_master_->Enqueue(std::move(commands));
    story_puppet_master_->Execute(
        [this](fuchsia::modular::ExecuteResult result) {
          story_create_.Pass();
          StartStory();
        });
  }

  TestPoint story_get_controller_{"Story GetController()"};
  // Starts the story and adds two modules to it.
  void StartStory() {
    story_provider()->GetController(kStoryName, story_controller_.NewRequest());
    story_controller_.set_error_handler([this](zx_status_t status) {
      FXL_LOG(ERROR) << "Story controller for story " << kStoryName
                     << " died. Does this story exist?";
    });

    story_controller_->RequestStart();
    story_controller_->GetInfo(
        [this](fuchsia::modular::StoryInfo, fuchsia::modular::StoryState) {
          story_get_controller_.Pass();
          PerformWatchActivity();
        });
  }

  TestPoint on_watch_ongoing_activities_dispatched{
      "When a watcher is registered, ongoing activities should be dispatched."};
  void PerformWatchActivity() {
    story_activity_watcher_.Watch(story_provider());
    story_activity_watcher_.OnNotify(
        [this](std::string story_id,
               std::vector<fuchsia::modular::OngoingActivityType> activities) {
          if (story_id == kStoryName && activities.empty()) {
            on_watch_ongoing_activities_dispatched.Pass();
          }
          PerformFirstModuleStartActivity();
        });
  }

  TestPoint on_start_ongoing_activity_dispatched{
      "When there is a new ongoing activity, the ongoing activity should be "
      "dispatched."};
  // Signals the first module to call ModuleContext.StartOngoingActivity().
  void PerformFirstModuleStartActivity() {
    Signal(kFirstModuleCallStartActivity);
    story_activity_watcher_.OnNotify(
        [this](std::string story_id,
               std::vector<fuchsia::modular::OngoingActivityType> activities) {
          if (story_id == kStoryName && activities.size() == 1 &&
              activities[0] == fuchsia::modular::OngoingActivityType::VIDEO) {
            on_start_ongoing_activity_dispatched.Pass();
          }
          PerformSecondModuleStartActivity();
        });
  }

  TestPoint on_start_all_ongoing_activities_dispatched{
      "When there is a new ongoing activity, all ongoing activities should be "
      "dispatched."};
  // Signals the second module to call ModuleContext.StartOngoingActivity().
  void PerformSecondModuleStartActivity() {
    Signal(kSecondModuleCallStartActivity);
    story_activity_watcher_.OnNotify(
        [this](std::string story_id,
               std::vector<fuchsia::modular::OngoingActivityType> activities) {
          if (story_id == kStoryName && activities.size() == 2 &&
              activities[0] == fuchsia::modular::OngoingActivityType::VIDEO &&
              activities[1] == fuchsia::modular::OngoingActivityType::VIDEO) {
            on_start_all_ongoing_activities_dispatched.Pass();
          }
          PerformSecondModuleStopActivity();
        });
  }

  TestPoint on_stop_remaining_ongoing_activities_dispatched{
      "When an ongoing activity is stopped, all remaining ongoing activities "
      "should be dispatched."};
  // Signals the second module to stop ongoing activity.
  void PerformSecondModuleStopActivity() {
    Signal(kSecondModuleCallStopActivity);
    story_activity_watcher_.OnNotify(
        [this](std::string story_id,
               std::vector<fuchsia::modular::OngoingActivityType> activities) {
          if (story_id == kStoryName && activities.size() == 1 &&
              activities[0] == fuchsia::modular::OngoingActivityType::VIDEO) {
            on_stop_remaining_ongoing_activities_dispatched.Pass();
          }
          TestModuleCreatingEntity();
        });
  }

  void TestModuleCreatingEntity() {
    Await(kEntityModuleDoneFirstTask, [this] {
      Await(kEntityModuleDoneSecondTask, [this] {
        fuchsia::modular::Intent intent;
        intent.handler = kEntityModuleUrl;
        intent.action = kEntityIntentAction;
        fuchsia::modular::RemoveMod remove_mod;
        remove_mod.mod_name_transitional = "entity_module";

        std::vector<fuchsia::modular::StoryCommand> commands;
        fuchsia::modular::StoryCommand command;
        command.set_remove_mod(std::move(remove_mod));
        commands.push_back(std::move(command));

        puppet_master_->ControlStory(kStoryName,
                                     story_puppet_master_.NewRequest());
        story_puppet_master_->Enqueue(std::move(commands));
        story_puppet_master_->Execute(
            [this](fuchsia::modular::ExecuteResult result) {
              PerformFirstModuleDone();
            });
      });
    });
  }

  TestPoint on_done_ongoing_activities_stopped{
      "When a module is teared down, the ongoing activity should also be "
      "stopped"};
  TestPoint second_module_active_{
      "Only second module is still active after first calls "
      "RemoveSelfFromStory()"};
  // Signals the first module to call ModuleContext.RemoveSelfFromStory().
  void PerformFirstModuleDone() {
    Signal(kFirstModuleCallDone);
    Await(kFirstModuleTerminated, [this] {
      // Verify that the second module is still active, but the first one is
      // not.
      story_controller_->GetActiveModules(
          [this](std::vector<fuchsia::modular::ModuleData> module_data) {
            if (module_data.size() == 1) {
              second_module_active_.Pass();
            }
            VerifyStoryStillRunning();
          });
    });

    story_activity_watcher_.OnNotify(
        [this](std::string story_id,
               std::vector<fuchsia::modular::OngoingActivityType> activities) {
          if (story_id == kStoryName && activities.empty()) {
            on_done_ongoing_activities_stopped.Pass();
          }
        });
  }

  TestPoint story_still_active_{
      "The story is still active after first module calls "
      "RemoveSelfFromStory()"};
  // Verifies that the story is still running after the first module has called
  // done and been stopped.
  void VerifyStoryStillRunning() {
    IsStoryRunning([this](bool is_running) {
      if (is_running) {
        story_still_active_.Pass();
      }
      PerformSecondModuleDone();
    });
  }

  TestPoint no_module_active_{
      "No modules are active after second mod calls RemoveSelfFromStory()"};
  TestPoint story_stopped_{"The story was stopped."};
  // Signals the second module to call ModuleContext.Done.
  void PerformSecondModuleDone() {
    Signal(kSecondModuleCallDone);
    Await(kSecondModuleTerminated, [this] {
      // Verify that the second module is still active.
      story_controller_->GetActiveModules(
          [this](std::vector<fuchsia::modular::ModuleData> module_data) {
            if (module_data.empty()) {
              no_module_active_.Pass();
            }
            IsStoryRunning([this](bool is_running) {
              if (!is_running) {
                story_stopped_.Pass();
              }

              Signal(modular::testing::kTestShutdown);
            });
          });
    });
  }

  // Verifies that the story is stopped when the last module that is part of the
  // story calls ModuleContext.Done and is stopped.
  void IsStoryRunning(fit::function<void(bool)> callback) {
    story_controller_->GetInfo([this, callback = std::move(callback)](
                                   fuchsia::modular::StoryInfo story_info,
                                   fuchsia::modular::StoryState state) {
      callback(state == fuchsia::modular::StoryState::RUNNING);
    });
  }

  // Creates an intent with one parameter, kLinkName, with the following
  // contents: { |kLinkKey| : |parameter_string| }.
  fuchsia::modular::Intent IntentWithParameterString(
      std::string parameter_string) {
    fuchsia::modular::Intent intent;
    intent.handler = kModuleUrl;
    intent.action = kIntentAction;

    fuchsia::modular::IntentParameter parameter;
    parameter.name = kLinkName;

    rapidjson::Document document;
    document.SetObject();
    document.AddMember(kLinkKey, parameter_string, document.GetAllocator());

    fuchsia::modular::IntentParameterData parameter_data;
    fsl::SizedVmo vmo;
    FXL_CHECK(fsl::VmoFromString(modular::JsonValueToString(document), &vmo));
    parameter_data.set_json(std::move(vmo).ToTransport());
    parameter.data = std::move(parameter_data);
    intent.parameters.push_back(std::move(parameter));

    return intent;
  }

  fuchsia::modular::PuppetMasterPtr puppet_master_;
  fuchsia::modular::StoryPuppetMasterPtr story_puppet_master_;
  fuchsia::modular::StoryControllerPtr story_controller_;
  StoryActivityWatcherImpl story_activity_watcher_;

  fxl::WeakPtrFactory<TestApp> weak_ptr_factory_;

  FXL_DISALLOW_COPY_AND_ASSIGN(TestApp);
};

}  // namespace

int main(int /*argc*/, const char** /*argv*/) {
  modular::testing::ComponentMain<TestApp>();
  return 0;
}
