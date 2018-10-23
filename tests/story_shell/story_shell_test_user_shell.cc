// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <set>
#include <utility>

#include <fuchsia/modular/cpp/fidl.h>
#include <fuchsia/sys/cpp/fidl.h>
#include <fuchsia/ui/policy/cpp/fidl.h>
#include <fuchsia/ui/viewsv1token/cpp/fidl.h>
#include <lib/async/cpp/task.h>
#include <lib/async/default.h>
#include <lib/component/cpp/startup_context.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/fxl/command_line.h>
#include <lib/fxl/logging.h>
#include <lib/fxl/macros.h>
#include <lib/fxl/tasks/task_runner.h>
#include <lib/fxl/time/time_delta.h>

#include "peridot/lib/common/names.h"
#include "peridot/lib/fidl/clone.h"
#include "peridot/lib/rapidjson/rapidjson.h"
#include "peridot/lib/testing/component_base.h"
#include "peridot/lib/testing/reporting.h"
#include "peridot/lib/testing/testing.h"
#include "peridot/tests/common/defs.h"
#include "peridot/tests/story_shell/defs.h"

using modular::testing::Get;
using modular::testing::Put;
using modular::testing::TestPoint;

namespace {

const char kStoryName1[] = "story1";
const char kStoryName2[] = "story2";

// Cf. README.md for what this test does and how.
class TestApp : public modular::testing::ComponentBase<void>,
                fuchsia::modular::UserShellPresentationProvider {
 public:
  explicit TestApp(component::StartupContext* const startup_context)
      : ComponentBase(startup_context) {
    TestInit(__FILE__);

    puppet_master_ =
        startup_context
            ->ConnectToEnvironmentService<fuchsia::modular::PuppetMaster>();
    user_shell_context_ =
        startup_context
            ->ConnectToEnvironmentService<fuchsia::modular::UserShellContext>();
    user_shell_context_->GetStoryProvider(story_provider_.NewRequest());

    startup_context->outgoing()
        .AddPublicService<fuchsia::modular::UserShellPresentationProvider>(
            [this](fidl::InterfaceRequest<
                   fuchsia::modular::UserShellPresentationProvider>
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

  // |fuchsia::modular::UserShellPresentationProvider|
  void GetPresentation(fidl::StringPtr story_id,
                       fidl::InterfaceRequest<fuchsia::ui::policy::Presentation>
                           request) override {
    if (story_id == kStoryName1 &&
        !story1_presentation_request_received_) {
      story1_presentation_request_.Pass();
      story1_presentation_request_received_ = true;
    }

    if (story_id == kStoryName2 &&
        !story2_presentation_request_received_) {
      story2_presentation_request_.Pass();
      story2_presentation_request_received_ = true;
    }

    MaybeLogout();
  }

  // |fuchsia::modular::UserShellPresentationProvider|
  void WatchVisualState(
      fidl::StringPtr story_id,
      fidl::InterfaceHandle<fuchsia::modular::StoryVisualStateWatcher> watcher)
      override {}

  TestPoint create_view_{"CreateView()"};

  // |SingleServiceApp|
  void CreateView(
      fidl::InterfaceRequest<
          fuchsia::ui::viewsv1token::ViewOwner> /*view_owner_request*/,
      fidl::InterfaceRequest<fuchsia::sys::ServiceProvider> /*services*/)
      override {
    create_view_.Pass();
  }

  TestPoint story1_create_{"Story1 Create"};

  void Story1_Create() {
    fidl::VectorPtr<fuchsia::modular::StoryCommand> commands;
    auto addMod = [&commands](fidl::StringPtr name,
                              std::vector<fidl::StringPtr> parent) {
      fuchsia::modular::AddMod add_mod;
      add_mod.mod_name.push_back(name);
      add_mod.intent.action = kCommonNullAction;
      add_mod.intent.handler = kCommonNullModule;
      add_mod.surface_parent_mod_name.resize(0);
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
    story_provider_->GetController(kStoryName1, story_controller_.NewRequest());

    // TODO(jphsiao|vardhan): remodel this |proceed_after_6| style of
    // continuation to use Futures instead.
    auto proceed_after_6 = modular::testing::NewBarrierClosure(6, [this] {
      story1_run1_.Pass();
      Story1_Stop1();
    });

    Get("story link data: null", proceed_after_6);
    Get("root:one", proceed_after_6);
    Get("root:one manifest", proceed_after_6);
    Get("root:one:two", proceed_after_6);
    Get("root:one:two manifest", proceed_after_6);
    Get("root:one:two ordering", proceed_after_6);

    fidl::InterfaceHandle<fuchsia::ui::viewsv1token::ViewOwner> story_view;
    story_controller_->Start(story_view.NewRequest());
  }

  void Story1_Stop1() {
    story_controller_->Stop([this] { Story1_Run2(); });
  }

  TestPoint story1_run2_{"Story1 Run2"};

  void Story1_Run2() {
    auto proceed_after_6 = modular::testing::NewBarrierClosure(6, [this] {
      story1_run2_.Pass();
      Story1_Stop2();
    });

    Get("story link data: {\"label\":\"value\"}", proceed_after_6);
    Get("root:one", proceed_after_6);
    Get("root:one manifest", proceed_after_6);
    Get("root:one:two", proceed_after_6);
    Get("root:one:two manifest", proceed_after_6);
    Get("root:one:two ordering", proceed_after_6);

    fidl::InterfaceHandle<fuchsia::ui::viewsv1token::ViewOwner> story_view;
    story_controller_->Start(story_view.NewRequest());
  }

  void Story1_Stop2() {
    story_controller_->Stop([this] { Story2_Create(); });
  }

  // We do the same sequence with Story2 that we did for Story1, except that the
  // modules are started with packages rather than actions in their Intents.

  TestPoint story2_create_{"Story2 Create"};

  void Story2_Create() {
    fidl::VectorPtr<fuchsia::modular::StoryCommand> commands;
    auto addMod = [&commands](fidl::StringPtr name,
                              std::vector<fidl::StringPtr> parent) {
      fuchsia::modular::AddMod add_mod;
      add_mod.mod_name.push_back(name);
      add_mod.intent.action = kCommonNullAction;
      add_mod.intent.handler = kCommonNullModule;
      add_mod.surface_parent_mod_name.resize(0);
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
    auto proceed_after_5 = modular::testing::NewBarrierClosure(5, [this] {
      story2_run1_.Pass();
      Story2_Stop1();
    });

    Get("root:one", proceed_after_5);
    Get("root:one manifest", proceed_after_5);
    Get("root:one:two", proceed_after_5);
    Get("root:one:two manifest", proceed_after_5);
    Get("root:one:two ordering", proceed_after_5);

    story_provider_->GetController(kStoryName2, story_controller_.NewRequest());
    fidl::InterfaceHandle<fuchsia::ui::viewsv1token::ViewOwner> story_view;
    story_controller_->Start(story_view.NewRequest());
  }

  void Story2_Stop1() {
    story_controller_->Stop([this] { Story2_Run2(); });
  }

  TestPoint story2_run2_{"Story2 Run2"};

  void Story2_Run2() {
    auto proceed_after_5 = modular::testing::NewBarrierClosure(5, [this] {
      story2_run2_.Pass();
      Story2_Stop2();
    });

    Get("root:one", proceed_after_5);
    Get("root:one manifest", proceed_after_5);
    Get("root:one:two", proceed_after_5);
    Get("root:one:two manifest", proceed_after_5);
    Get("root:one:two ordering", proceed_after_5);

    fidl::InterfaceHandle<fuchsia::ui::viewsv1token::ViewOwner> story_view;
    story_controller_->Start(story_view.NewRequest());
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
      user_shell_context_->Logout();
    }
  }

  fuchsia::modular::PuppetMasterPtr puppet_master_;
  fuchsia::modular::StoryPuppetMasterPtr story_puppet_master_;
  fuchsia::modular::UserShellContextPtr user_shell_context_;
  fuchsia::modular::StoryProviderPtr story_provider_;
  fuchsia::modular::StoryControllerPtr story_controller_;
  fidl::BindingSet<fuchsia::modular::UserShellPresentationProvider>
      presentation_provider_bindings_;

  FXL_DISALLOW_COPY_AND_ASSIGN(TestApp);
};

}  // namespace

int main(int /* argc */, const char** /* argv */) {
  modular::testing::ComponentMain<TestApp>();
  return 0;
}
