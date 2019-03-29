// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <set>
#include <utility>

#include <fuchsia/modular/cpp/fidl.h>
#include <fuchsia/sys/cpp/fidl.h>
#include <fuchsia/ui/policy/cpp/fidl.h>
#include <lib/async/cpp/future.h>
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
#include "peridot/tests/story_shell/defs.h"

using modular::testing::Get;
using modular::testing::Put;
using modular::testing::Signal;
using modular::testing::TestPoint;

namespace {

const char kStoryName1[] = "story1";
const char kStoryName2[] = "story2";

// Cf. README.md for what this test does and how.
class TestApp : public modular::testing::SessionShellBase,
                fuchsia::modular::SessionShellPresentationProvider {
 public:
  explicit TestApp(component::StartupContext* const startup_context)
      : SessionShellBase(startup_context) {
    TestInit(__FILE__);

    startup_context->ConnectToEnvironmentService(puppet_master_.NewRequest());

    startup_context->outgoing()
        .AddPublicService<fuchsia::modular::SessionShellPresentationProvider>(
            [this](fidl::InterfaceRequest<
                   fuchsia::modular::SessionShellPresentationProvider>
                       request) {
              presentation_provider_bindings_.AddBinding(this,
                                                         std::move(request));
            });

    Story1_Create();
  }

  ~TestApp() override = default;

 private:
  TestPoint story1_presentation_request_{"Story1 Presentation request"};
  bool story1_presentation_request_received_{};

  TestPoint story2_presentation_request_{"Story2 Presentation request"};
  bool story2_presentation_request_received_{};

  // |fuchsia::modular::SessionShellPresentationProvider|
  void GetPresentation(std::string story_id,
                       fidl::InterfaceRequest<fuchsia::ui::policy::Presentation>
                           request) override {
    if (story_id == kStoryName1 && !story1_presentation_request_received_) {
      story1_presentation_request_.Pass();
      story1_presentation_request_received_ = true;
    }

    if (story_id == kStoryName2 && !story2_presentation_request_received_) {
      story2_presentation_request_.Pass();
      story2_presentation_request_received_ = true;
    }

    MaybeLogout();
  }

  // |fuchsia::modular::SessionShellPresentationProvider|
  void WatchVisualState(
      std::string story_id,
      fidl::InterfaceHandle<fuchsia::modular::StoryVisualStateWatcher> watcher)
      override {}

  TestPoint create_view_{"CreateView()"};

  // |SingleServiceApp|
  void CreateView(
      zx::eventpair /*view_token*/,
      fidl::InterfaceRequest<
          fuchsia::sys::ServiceProvider> /*incoming_services*/,
      fidl::InterfaceHandle<
          fuchsia::sys::ServiceProvider> /*outgoing_services*/) override {
    create_view_.Pass();
  }

  TestPoint story1_create_{"Story1 Create"};

  void Story1_Create() {
    std::vector<fuchsia::modular::StoryCommand> commands;
    auto addMod = [&commands](fidl::StringPtr name,
                              std::vector<fidl::StringPtr> parent) {
      fuchsia::modular::AddMod add_mod;
      add_mod.mod_name_transitional = name;
      add_mod.intent.action = kCommonNullAction;
      add_mod.intent.handler = kCommonNullModule;

      for (auto i : parent) {
        add_mod.surface_parent_mod_name.push_back(i);
      }

      fuchsia::modular::StoryCommand command;
      command.set_add_mod(std::move(add_mod));
      commands.push_back(std::move(command));
    };

    addMod("root", {});
    addMod("one", {"root"});
    addMod("two", {"root", "one"});

    puppet_master_->ControlStory(kStoryName1,
                                 story_puppet_master_.NewRequest());
    story_puppet_master_->Enqueue(std::move(commands));
    story_puppet_master_->Execute(
        [this](fuchsia::modular::ExecuteResult result) {
          story1_create_.Pass();
          Story1_Run1();
        });
  }

  TestPoint story1_run1_{"Story1 Run1"};

  void Story1_Run1() {
    story_provider()->GetController(kStoryName1,
                                    story_controller_.NewRequest());

    std::vector<modular::FuturePtr<fidl::StringPtr>> fgets;

    Get("story link data: null", modular::testing::AddBarrierFuture(fgets));
    Get("story link data: null", modular::testing::AddBarrierFuture(fgets));
    Get("root:one", modular::testing::AddBarrierFuture(fgets));
    Get("root:one manifest", modular::testing::AddBarrierFuture(fgets));
    Get("root:one:two", modular::testing::AddBarrierFuture(fgets));
    Get("root:one:two manifest", modular::testing::AddBarrierFuture(fgets));
    Get("root:one:two ordering", modular::testing::AddBarrierFuture(fgets));

    story_controller_->RequestStart();

    // |Wait| can execute its |Future| elements immediately, so to make
    // sure the "Stop" calls happen after the "Start", position the Wait
    // call accordingly.
    Wait("future Get results", fgets)
        ->Then([this](std::vector<fidl::StringPtr> all_results) {
          story1_run1_.Pass();
          Story1_Stop1();
        });
  }

  void Story1_Stop1() {
    story_controller_->Stop([this] { Story1_Run2(); });
  }

  TestPoint story1_run2_{"Story1 Run2"};

  void Story1_Run2() {
    std::vector<modular::FuturePtr<fidl::StringPtr>> fgets;

    Get("story link data: {\"label\":\"value\"}",
        modular::testing::AddBarrierFuture(fgets));
    Get("root:one", modular::testing::AddBarrierFuture(fgets));
    Get("root:one manifest", modular::testing::AddBarrierFuture(fgets));
    Get("root:one:two", modular::testing::AddBarrierFuture(fgets));
    Get("root:one:two manifest", modular::testing::AddBarrierFuture(fgets));
    Get("root:one:two ordering", modular::testing::AddBarrierFuture(fgets));

    story_controller_->RequestStart();

    // |Wait| can call |Story1Stop2| immediately and must AFTER |RequestStart|
    Wait("future Get results", fgets)
        ->Then([this](std::vector<fidl::StringPtr> all_results) {
          story1_run2_.Pass();
          Story1_Stop2();
        });
  }

  void Story1_Stop2() {
    story_controller_->Stop([this] { Story2_Create(); });
  }

  // We do the same sequence with Story2 that we did for Story1, except that the
  // modules are started with packages rather than actions in their Intents.

  TestPoint story2_create_{"Story2 Create"};

  void Story2_Create() {
    std::vector<fuchsia::modular::StoryCommand> commands;
    auto addMod = [&commands](fidl::StringPtr name,
                              std::vector<fidl::StringPtr> parent) {
      fuchsia::modular::AddMod add_mod;
      add_mod.mod_name_transitional = name;
      add_mod.intent.action = kCommonNullAction;
      add_mod.intent.handler = kCommonNullModule;

      for (auto i : parent) {
        add_mod.surface_parent_mod_name.push_back(i);
      }

      fuchsia::modular::StoryCommand command;
      command.set_add_mod(std::move(add_mod));
      commands.push_back(std::move(command));
    };

    addMod("root", {});
    addMod("one", {"root"});
    addMod("two", {"root", "one"});

    puppet_master_->ControlStory(kStoryName2,
                                 story_puppet_master_.NewRequest());
    story_puppet_master_->Enqueue(std::move(commands));
    story_puppet_master_->Execute(
        [this](fuchsia::modular::ExecuteResult result) {
          story2_create_.Pass();
          Story2_Run1();
        });
  }

  TestPoint story2_run1_{"Story2 Run1"};

  void Story2_Run1() {
    std::vector<modular::FuturePtr<fidl::StringPtr>> fgets;

    Get("root:one", modular::testing::AddBarrierFuture(fgets));
    Get("root:one manifest", modular::testing::AddBarrierFuture(fgets));
    Get("root:one:two", modular::testing::AddBarrierFuture(fgets));
    Get("root:one:two manifest", modular::testing::AddBarrierFuture(fgets));
    Get("root:one:two ordering", modular::testing::AddBarrierFuture(fgets));

    story_provider()->GetController(kStoryName2,
                                    story_controller_.NewRequest());
    story_controller_->RequestStart();

    // |Wait| can call |Story1Stop2| immediately and must AFTER |RequestStart|
    Wait("future Get results", fgets)
        ->Then([this](std::vector<fidl::StringPtr> all_results) {
          story2_run1_.Pass();
          Story2_Stop1();
        });
  }

  void Story2_Stop1() {
    story_controller_->Stop([this] { Story2_Run2(); });
  }

  TestPoint story2_run2_{"Story2 Run2"};

  void Story2_Run2() {
    std::vector<modular::FuturePtr<fidl::StringPtr>> fgets;

    Get("root:one", modular::testing::AddBarrierFuture(fgets));
    Get("root:one manifest", modular::testing::AddBarrierFuture(fgets));
    Get("root:one:two", modular::testing::AddBarrierFuture(fgets));
    Get("root:one:two manifest", modular::testing::AddBarrierFuture(fgets));
    Get("root:one:two ordering", modular::testing::AddBarrierFuture(fgets));

    story_controller_->RequestStart();

    // |Wait| can call |Story1Stop2| immediately and must AFTER |RequestStart|
    Wait("future Get results", fgets)
        ->Then([this](std::vector<fidl::StringPtr> all_results) {
          story2_run2_.Pass();
          Story2_Stop2();
        });
  }

  bool end_of_story2_{};

  void Story2_Stop2() {
    story_controller_->Stop([this] {
      end_of_story2_ = true;
      MaybeLogout();
    });
  }

  void MaybeLogout() {
    if (story1_presentation_request_received_ &&
        story2_presentation_request_received_ && end_of_story2_) {
      Signal(modular::testing::kTestShutdown);
    }
  }

  fuchsia::modular::PuppetMasterPtr puppet_master_;
  fuchsia::modular::StoryPuppetMasterPtr story_puppet_master_;
  fuchsia::modular::StoryControllerPtr story_controller_;
  fidl::BindingSet<fuchsia::modular::SessionShellPresentationProvider>
      presentation_provider_bindings_;

  FXL_DISALLOW_COPY_AND_ASSIGN(TestApp);
};

}  // namespace

int main(int /* argc */, const char** /* argv */) {
  modular::testing::ComponentMain<TestApp>();
  return 0;
}
