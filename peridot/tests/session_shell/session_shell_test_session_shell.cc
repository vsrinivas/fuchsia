// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/app/discover/cpp/fidl.h>
#include <fuchsia/modular/cpp/fidl.h>
#include <fuchsia/sys/cpp/fidl.h>
#include <lib/async/cpp/task.h>
#include <lib/async/default.h>
#include <lib/component/cpp/startup_context.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/fsl/vmo/sized_vmo.h>
#include <lib/fsl/vmo/strings.h>
#include <src/lib/fxl/command_line.h>
#include <src/lib/fxl/logging.h>
#include <src/lib/fxl/macros.h>
#include <src/lib/fxl/time/time_delta.h>

#include <memory>
#include <set>
#include <utility>

#include "peridot/lib/rapidjson/rapidjson.h"
#include "peridot/lib/testing/component_main.h"
#include "peridot/lib/testing/session_shell_base.h"
#include "peridot/public/lib/integration_testing/cpp/reporting.h"
#include "peridot/public/lib/integration_testing/cpp/testing.h"
#include "peridot/tests/common/defs.h"
#include "peridot/tests/session_shell/defs.h"

using modular::testing::Await;
using modular::testing::Fail;
using modular::testing::Signal;
using modular::testing::TestPoint;

using fuchsia::modular::ViewIdentifier;

namespace {

// A simple story provider watcher implementation. Just logs observed state
// transitions.
class StoryProviderStateWatcherImpl : fuchsia::modular::StoryProviderWatcher {
 public:
  StoryProviderStateWatcherImpl() : binding_(this) {}
  ~StoryProviderStateWatcherImpl() override = default;

  // Registers itself a watcher on the given story provider. Only one story
  // provider can be watched at a time.
  void Watch(fuchsia::modular::StoryProvider* const story_provider) {
    story_provider->Watch(binding_.NewBinding());
  }

  // Deregisters itself from the watched story provider.
  void Reset() { binding_.Unbind(); }

  void SetKindOfProtoStory(fidl::StringPtr story_id) {
    kind_of_proto_stories_.insert(story_id);
  }

 private:
  TestPoint on_delete_called_once_{"OnDelete() Called"};
  int on_delete_called_{};

  // |fuchsia::modular::StoryProviderWatcher|
  void OnDelete(std::string story_id) override {
    FXL_LOG(INFO) << "StoryProviderStateWatcherImpl::OnDelete() " << story_id;

    if (++on_delete_called_ == 1) {
      on_delete_called_once_.Pass();
    }

    deleted_stories_.emplace(story_id);
  }

  TestPoint on_running_called_once_{"OnChange() RUNNING Called"};
  int on_running_called_{};

  TestPoint on_stopping_called_once_{"OnChange() STOPPING Called"};
  int on_stopping_called_{};

  TestPoint on_stopped_called_once_{"OnChange() STOPPED Called"};
  int on_stopped_called_{};

  // |fuchsia::modular::StoryProviderWatcher|
  void OnChange(const fuchsia::modular::StoryInfo story_info,
                const fuchsia::modular::StoryState story_state,
                const fuchsia::modular::StoryVisibilityState
                    story_visibility_state) override {
    FXL_LOG(INFO) << "StoryProviderStateWatcherImpl::OnChange() "
                  << " id " << story_info.id << " state "
                  << fidl::ToUnderlying(story_state) << " visibility state "
                  << fidl::ToUnderlying(story_visibility_state) << " url "
                  << story_info.url;

    if (deleted_stories_.find(story_info.id) != deleted_stories_.end()) {
      FXL_LOG(ERROR) << "Status change notification for deleted story "
                     << story_info.id;
      modular::testing::Fail("Status change notification for deleted story");
    }

    if (kind_of_proto_stories_.find(story_info.id) !=
        kind_of_proto_stories_.end()) {
      modular::testing::Fail(
          "Stories with kind_of_proto_story option set shouldn't notify "
          "OnChange");
    }

    // Just check that all states are covered at least once, proving that we get
    // state notifications at all from the story provider.
    switch (story_state) {
      case fuchsia::modular::StoryState::RUNNING:
        if (++on_running_called_ == 1) {
          on_running_called_once_.Pass();
        }
        break;
      case fuchsia::modular::StoryState::STOPPING:
        if (++on_stopping_called_ == 1) {
          on_stopping_called_once_.Pass();
        }
        break;
      case fuchsia::modular::StoryState::STOPPED:
        if (++on_stopped_called_ == 1) {
          on_stopped_called_once_.Pass();
        }
        break;
    }
  }

  fidl::Binding<fuchsia::modular::StoryProviderWatcher> binding_;

  // Remember deleted stories. After a story is deleted, there must be no state
  // change notifications for it.
  std::set<std::string> deleted_stories_;

  std::set<std::string> kind_of_proto_stories_;

  FXL_DISALLOW_COPY_AND_ASSIGN(StoryProviderStateWatcherImpl);
};

// Cf. README.md for what this test does in general and how. The test cases are
// described in detail in comments below.
class TestApp : public modular::testing::SessionShellBase {
 public:
  using ViewId = fuchsia::modular::ViewIdentifier;

  explicit TestApp(component::StartupContext* const startup_context)
      : SessionShellBase(startup_context) {
    TestInit(__FILE__);

    // Just a health check for suggestion service connection.
    // TODO: exercise this service.
    startup_context->ConnectToEnvironmentService(
        suggestions_service_.NewRequest());
    if (suggestions_service_.is_bound()) {
      suggestions_service_connected_.Pass();
    } else {
      modular::testing::Fail("Failed to connect to suggestion service");
    }

    startup_context->ConnectToEnvironmentService(puppet_master_.NewRequest());
    startup_context->ConnectToEnvironmentService(
        component_context_.NewRequest());
    story_provider_state_watcher_.Watch(story_provider());

    TestComponentContext_GetPackageName_Works();
  }

  ~TestApp() override = default;

 private:
  TestPoint create_view_{"CreateView()"};
  TestPoint suggestions_service_connected_{"Suggestions service connected"};

  // |SingleServiceApp|
  void CreateView(
      zx::eventpair /*view_token*/,
      fidl::InterfaceRequest<
          fuchsia::sys::ServiceProvider> /*incoming_services*/,
      fidl::InterfaceHandle<
          fuchsia::sys::ServiceProvider> /*outgoing_services*/) override {
    create_view_.Pass();
  }

  // Test Case GetPackageName:
  //
  // When we call GetPackageName() on ComponentContext acquired from our
  // environment, it works.

  TestPoint component_context_package_name_{
      "ComponentContext.GetPackageName() works"};

  void TestComponentContext_GetPackageName_Works() {
    component_context_->GetPackageName([this](fidl::StringPtr name) {
      if (name) {
        component_context_package_name_.Pass();
      }

      TestStoryProvider_GetStoryInfo_Null();
    });
  }

  // Test Case GetStoryInfo Null:
  //
  // The story info of a story that does not exist is null.

  TestPoint get_story_info_null_{"StoryProvider.GetStoryInfo() is null"};

  void TestStoryProvider_GetStoryInfo_Null() {
    story_provider()->GetStoryInfo(
        "X", [this](fuchsia::modular::StoryInfoPtr story_info) {
          if (!story_info) {
            get_story_info_null_.Pass();
          }

          TestSessionShellContext_GetLink();
        });
  }

  // Test Case SessionShellContext:
  //
  // The session shell can access a Link.

  TestPoint get_link_{"SessionShellContext.GetLink()"};

  void TestSessionShellContext_GetLink() {
    session_shell_context()->GetLink(session_shell_link_.NewRequest());
    session_shell_link_->Get(
        nullptr, [this](std::unique_ptr<fuchsia::mem::Buffer> value) {
          get_link_.Pass();
          TestStoryProvider_GetStories();
        });
  }

  // Test Case StoryProvider:
  //
  // The session shell can access the list of existing stories. This list is
  // empty at the outset.

  TestPoint previous_stories_{"StoryProvider.GetStories()"};

  void TestStoryProvider_GetStories() {
    story_provider()->GetStories(
        nullptr, [this](std::vector<fuchsia::modular::StoryInfo> stories) {
          previous_stories_.Pass();
          TestStoryProvider_GetStoryInfo(std::move(stories));
        });
  }

  TestPoint get_story_info_{"StoryProvider.GetStoryInfo()"};

  void TestStoryProvider_GetStoryInfo(
      std::vector<fuchsia::modular::StoryInfo> stories) {
    if (stories.empty()) {
      get_story_info_.Pass();
    } else {
      FXL_LOG(ERROR) << "StoryProvider.GetStoryInfo() " << stories.size();
      for (const auto& item : stories) {
        FXL_LOG(INFO) << item.id;
      }
    }

    TestStory1();
  }

  // Test Case Story1:
  //
  // Create a story with extra information, start, and stop it.

  TestPoint story1_create_{"Story1 Create"};

  void TestStory1() {
    const std::string initial_json = R"({"created-with-info": true})";
    puppet_master_->ControlStory("story1", story_puppet_master_.NewRequest());

    fuchsia::modular::AddMod add_mod;
    add_mod.mod_name_transitional = "mod1";
    add_mod.intent.handler = kCommonActiveModule;

    fuchsia::modular::IntentParameter param;
    param.name = "root";
    fsl::SizedVmo vmo;
    FXL_CHECK(fsl::VmoFromString(initial_json, &vmo));
    param.data.set_json(std::move(vmo).ToTransport());
    add_mod.intent.parameters.push_back(std::move(param));

    fuchsia::modular::StoryCommand command;
    command.set_add_mod(std::move(add_mod));

    std::vector<fuchsia::modular::StoryCommand> commands;
    commands.push_back(std::move(command));

    story_puppet_master_->Enqueue(std::move(commands));
    story_puppet_master_->Execute(
        [this](fuchsia::modular::ExecuteResult result) {
          story1_create_.Pass();
          TestStory1_GetController("story1");
        });
  }

  TestPoint story1_get_controller_{"Story1 GetController"};

  void TestStory1_GetController(fidl::StringPtr story_id) {
    story_provider()->GetController(story_id, story_controller_.NewRequest());
    story_controller_->GetInfo([this](fuchsia::modular::StoryInfo story_info,
                                      fuchsia::modular::StoryState state) {
      story1_get_controller_.Pass();
      story_info_ = std::move(story_info);
      TestStory1_CheckRunning();
    });
  }

  TestPoint story1_run_{"Story1 Run"};
  void TestStory1_CheckRunning() {
    // Start and show the new story.
    story1_run_.Pass();
    TestStory1_Stop();
  }

  TestPoint story1_stop_{"Story1 Stop"};

  void TestStory1_Stop() {
    story_controller_->Stop([this] {
      TeardownStoryController();
      story1_stop_.Pass();

      // When the story is done, we start the next one.
      TestStory2();
    });
  }

  // Test Case Story2:
  //
  // Verify that when pipelining Start() and GetInfo() calls, GetInfo() yields
  // the run state after Start().
  //
  // Verify that after DeleteStory(), GetInfo() returns null again.

  TestPoint story2_create_{"Story2 Create"};

  void TestStory2() {
    puppet_master_->ControlStory("story2", story_puppet_master_.NewRequest());

    fuchsia::modular::AddMod add_mod;
    add_mod.mod_name_transitional = "mod1";
    add_mod.intent.handler = kCommonNullModule;

    fuchsia::modular::StoryCommand command;
    command.set_add_mod(std::move(add_mod));

    std::vector<fuchsia::modular::StoryCommand> commands;
    commands.push_back(std::move(command));

    story_puppet_master_->Enqueue(std::move(commands));
    story_puppet_master_->Execute(
        [this](fuchsia::modular::ExecuteResult result) {
          story2_create_.Pass();
          TestStory2_GetController("story2");
        });
  }

  TestPoint story2_get_controller_{"Story2 Get Controller"};

  void TestStory2_GetController(fidl::StringPtr story_id) {
    story_provider()->GetController(story_id, story_controller_.NewRequest());
    story_controller_->GetInfo([this](fuchsia::modular::StoryInfo story_info,
                                      fuchsia::modular::StoryState state) {
      story_info_ = std::move(story_info);
      story2_get_controller_.Pass();
      TestStory2_GetModules();
    });
  }

  TestPoint story2_get_modules_{"Story2 Get Modules"};

  void TestStory2_GetModules() {
    story_controller_->GetModules(
        [this](std::vector<fuchsia::modular::ModuleData> modules) {
          if (modules.size() == 1) {
            story2_get_modules_.Pass();
          }

          TestStory2_CheckRunning();
        });
  }

  TestPoint story2_state_after_run_{"Story2 State after Run"};

  void TestStory2_CheckRunning() {
    story_controller_->GetInfo([this](fuchsia::modular::StoryInfo info,
                                      fuchsia::modular::StoryState state) {
      if (state == fuchsia::modular::StoryState::RUNNING) {
        story2_state_after_run_.Pass();
      }

      TestStory2_DeleteStory();
    });
  }

  TestPoint story2_delete_{"Story2 Delete"};

  void TestStory2_DeleteStory() {
    puppet_master_->DeleteStory(story_info_.id,
                                [this] { story2_delete_.Pass(); });

    story_provider()->GetStoryInfo(
        story_info_.id, [this](fuchsia::modular::StoryInfoPtr info) {
          TestStory2_InfoAfterDeleteIsNull(std::move(info));
        });
  }

  TestPoint story2_info_after_delete_{"Story2 Info After Delete"};

  void TestStory2_InfoAfterDeleteIsNull(fuchsia::modular::StoryInfoPtr info) {
    story2_info_after_delete_.Pass();
    if (info) {
      modular::testing::Fail("StoryInfo after DeleteStory() must return null.");
    }

    TestStory3();
  }

  // Test Case Story3:
  //
  // Verify that a "kind of proto" story doesn't appear in the list of stories
  // of the story provider.

  TestPoint story3_create_{"Story3 Create"};

  void TestStory3() {
    story_provider_state_watcher_.Reset();
    story_provider_state_watcher_.Watch(story_provider());

    puppet_master_->ControlStory("story3", story_puppet_master_.NewRequest());

    fuchsia::modular::StoryOptions story_options;
    story_options.kind_of_proto_story = true;
    story_puppet_master_->SetCreateOptions(std::move(story_options));

    story_puppet_master_->Execute(
        [this](fuchsia::modular::ExecuteResult result) {
          story_provider_state_watcher_.SetKindOfProtoStory("story3");
          story3_create_.Pass();
          TestStory3_GetController("story3");
        });
  }

  TestPoint story3_get_controller_{"Story3 GetController"};

  void TestStory3_GetController(fidl::StringPtr story_id) {
    story_provider()->GetController(story_id, story_controller_.NewRequest());
    story_controller_->GetInfo([this](fuchsia::modular::StoryInfo story_info,
                                      fuchsia::modular::StoryState state) {
      story_info_ = std::move(story_info);
      story3_get_controller_.Pass();
      TestStory3_GetStories();
    });
  }

  TestPoint story3_previous_stories_{"Story3 GetGetStories"};

  void TestStory3_GetStories() {
    story_provider()->GetStories(
        nullptr, [this](std::vector<fuchsia::modular::StoryInfo> stories) {
          // Since this is a kind-of-proto story, it shouldn't appear in
          // GetStories calls. Note that we still expect 1 story to be here
          // since Story1 wasn't deleted.
          if (stories.size() == 1 && stories.at(0).id != story_info_.id) {
            story3_previous_stories_.Pass();
          } else {
            FXL_LOG(ERROR) << "StoryProvider.GetStories() " << stories.size();
            for (const auto& item : stories) {
              FXL_LOG(INFO) << item.id;
            }
          }
          TestStory3_CheckRunning();
        });
  }

  TestPoint story3_run_{"Story3 Run"};

  void TestStory3_CheckRunning() {
    story_controller_->GetInfo([this](fuchsia::modular::StoryInfo info,
                                      fuchsia::modular::StoryState state) {
      if (state == fuchsia::modular::StoryState::RUNNING) {
        story3_run_.Pass();
      }

      TestStory3_Stop();
    });
  }

  TestPoint story3_stop_{"Story3 Stop"};

  void TestStory3_Stop() {
    story_controller_->Stop([this] {
      TeardownStoryController();
      story3_stop_.Pass();
      TestStory3_DeleteStory();
    });
  }

  TestPoint story3_delete_{"Story3 Delete"};

  void TestStory3_DeleteStory() {
    puppet_master_->DeleteStory(story_info_.id,
                                [this] { story3_delete_.Pass(); });

    story_provider()->GetStoryInfo(
        story_info_.id, [this](fuchsia::modular::StoryInfoPtr info) {
          TestStory3_InfoAfterDeleteIsNull(std::move(info));
        });
  }

  TestPoint story3_info_after_delete_{"Story3 InfoAfterDeleteIsNull"};

  void TestStory3_InfoAfterDeleteIsNull(fuchsia::modular::StoryInfoPtr info) {
    if (!info) {
      story3_info_after_delete_.Pass();
    }

    TestStory4();
  }

  // Test Case Story4:
  //
  // Create a story; it should start running automatically.
  //
  // Verify the view is received through SessionShell.AttachView().
  //
  // Verify that, when the story is stopped, a request for
  // SessionShell.DetachView() is received.

  TestPoint story4_create_{"Story4 Create"};

  void TestStory4() {
    puppet_master_->ControlStory("story4", story_puppet_master_.NewRequest());

    fuchsia::modular::AddMod add_mod;
    add_mod.mod_name_transitional = "mod1";
    add_mod.intent.handler = kCommonNullModule;

    fuchsia::modular::StoryCommand command;
    command.set_add_mod(std::move(add_mod));

    std::vector<fuchsia::modular::StoryCommand> commands;
    commands.push_back(std::move(command));

    story_puppet_master_->Enqueue(std::move(commands));

    session_shell_impl()->set_on_attach_view(
        [this](ViewId) { story4_attach_view_.Pass(); });

    story_puppet_master_->Execute(
        [this](fuchsia::modular::ExecuteResult result) {
          story4_create_.Pass();
          TestStory4_CheckRunning();
        });
  }

  TestPoint story4_state_after_run_{"Story4 State after Run"};
  TestPoint story4_attach_view_{"Story4 attach View"};

  void TestStory4_CheckRunning() {
    story_provider()->GetController("story4", story_controller_.NewRequest());
    story_controller_->GetInfo([this](fuchsia::modular::StoryInfo info,
                                      fuchsia::modular::StoryState state) {
      if (state == fuchsia::modular::StoryState::RUNNING) {
        story4_state_after_run_.Pass();
        TestStory4_Stop();
      }
    });
  }

  TestPoint story4_detach_view_{"Story4 detach View"};
  TestPoint story4_stop_{"Story4 Stop"};

  void TestStory4_Stop() {
    session_shell_impl()->set_on_detach_view(
        [this](ViewId) { story4_detach_view_.Pass(); });

    story_controller_->Stop([this] {
      TeardownStoryController();
      story4_stop_.Pass();

      TestStory4_DeleteStory();
    });
  }

  TestPoint story4_delete_{"Story4 Delete"};

  void TestStory4_DeleteStory() {
    puppet_master_->DeleteStory(story_info_.id,
                                [this] { story4_delete_.Pass(); });

    story_provider()->GetStoryInfo(
        story_info_.id, [this](fuchsia::modular::StoryInfoPtr info) {
          TestStory4_InfoAfterDeleteIsNull(std::move(info));
        });
  }

  TestPoint story4_info_after_delete_{"Story4 Info after Delete is null"};

  void TestStory4_InfoAfterDeleteIsNull(fuchsia::modular::StoryInfoPtr info) {
    if (!info) {
      story4_info_after_delete_.Pass();
    }

    TestStory5();
  }

  // Test Case Story5:
  //
  // Create a story; it should start running automatically.
  //
  // Verify that, when the story is stopped, a request for
  // SessionShell.DetachView() is received, and if the request is not answered,
  // the Stop() request proceeds anyway.

  TestPoint story5_create_{"Story5 Create"};

  void TestStory5() {
    puppet_master_->ControlStory("story5", story_puppet_master_.NewRequest());

    fuchsia::modular::AddMod add_mod;
    add_mod.mod_name_transitional = "mod1";
    add_mod.intent.handler = kCommonNullModule;

    fuchsia::modular::StoryCommand command;
    command.set_add_mod(std::move(add_mod));

    std::vector<fuchsia::modular::StoryCommand> commands;
    commands.push_back(std::move(command));

    story_puppet_master_->Enqueue(std::move(commands));

    session_shell_impl()->set_on_attach_view(
        [this](ViewId) { story5_attach_view_.Pass(); });

    story_puppet_master_->Execute(
        [this](fuchsia::modular::ExecuteResult result) {
          story5_create_.Pass();
          TestStory5_CheckRunning();
        });
  }

  TestPoint story5_state_after_run_{"Story5 State after Run"};

  TestPoint story5_attach_view_{"Story5 attach View"};

  void TestStory5_CheckRunning() {
    story_provider()->GetController("story5", story_controller_.NewRequest());
    story_controller_->GetInfo([this](fuchsia::modular::StoryInfo info,
                                      fuchsia::modular::StoryState state) {
      if (state == fuchsia::modular::StoryState::RUNNING) {
        story5_state_after_run_.Pass();
        TestStory5_Stop();
      }
    });
  }

  TestPoint story5_stop_{"Story5 Stop"};

  void TestStory5_Stop() {
    // Ignore the detach view. The delay is larger than the timeout for the
    // whole test configured in dev_base_shell.cc, so an attempt to wait for
    // this timeout would fail the whole test.
    session_shell_impl()->set_detach_delay(
        zx::msec(modular::testing::kTestTimeoutMilliseconds * 2));
    session_shell_impl()->set_on_detach_view([](ViewId) {});

    story_controller_->Stop([this] {
      TeardownStoryController();
      story5_stop_.Pass();

      TestStory5_DeleteStory();
    });
  }

  TestPoint story5_delete_{"Story5 Delete"};

  void TestStory5_DeleteStory() {
    puppet_master_->DeleteStory(story_info_.id,
                                [this] { story5_delete_.Pass(); });

    story_provider()->GetStoryInfo(
        story_info_.id, [this](fuchsia::modular::StoryInfoPtr info) {
          TestStory5_InfoAfterDeleteIsNull(std::move(info));
        });
  }

  TestPoint story5_info_after_delete_{"Story5 Info after Delete is null"};

  void TestStory5_InfoAfterDeleteIsNull(fuchsia::modular::StoryInfoPtr info) {
    if (!info) {
      story5_info_after_delete_.Pass();
    }

    TestStory6();
  }

  // Test Case Story6:
  //
  // Create a story; it should start running automatically.
  //
  // Verify that, when the story is NOT stopped when the SessionShell is stopped
  // (such as at Logout) NO request for SessionShell.DetachView() is received.

  TestPoint story6_create_{"Story6 Create"};

  void TestStory6() {
    puppet_master_->ControlStory("story6", story_puppet_master_.NewRequest());

    fuchsia::modular::AddMod add_mod;
    add_mod.mod_name_transitional = "mod1";
    add_mod.intent.handler = kCommonNullModule;

    fuchsia::modular::StoryCommand command;
    command.set_add_mod(std::move(add_mod));

    std::vector<fuchsia::modular::StoryCommand> commands;
    commands.push_back(std::move(command));

    story_puppet_master_->Enqueue(std::move(commands));
    session_shell_impl()->set_on_attach_view(
        [this](ViewId) { story6_attach_view_.Pass(); });
    story_puppet_master_->Execute(
        [this](fuchsia::modular::ExecuteResult result) {
          story6_create_.Pass();
          TestStory6_CheckRunning();
        });
  }

  TestPoint story6_state_after_run_{"Story6 State after Run"};

  TestPoint story6_attach_view_{"Story6 attach View"};

  void TestStory6_CheckRunning() {
    story_provider()->GetController("story6", story_controller_.NewRequest());
    story_controller_->GetInfo([this](fuchsia::modular::StoryInfo info,
                                      fuchsia::modular::StoryState state) {
      if (state == fuchsia::modular::StoryState::RUNNING) {
        story6_state_after_run_.Pass();
        TestStory6_Logout();
      }
    });
  }

  void TestStory6_Logout() {
    // If we get a DetachView() call during logout, that's a failure.
    session_shell_impl()->set_detach_delay(zx::sec(0));
    session_shell_impl()->set_on_detach_view(
        [](ViewId) { Fail("DetachView() Received on Logout"); });

    Signal(modular::testing::kTestShutdown);
  }

  void TeardownStoryController() { story_controller_.Unbind(); }

  StoryProviderStateWatcherImpl story_provider_state_watcher_;

  fuchsia::app::discover::SuggestionsPtr suggestions_service_;
  fuchsia::modular::PuppetMasterPtr puppet_master_;
  fuchsia::modular::ComponentContextPtr component_context_;
  fuchsia::modular::StoryPuppetMasterPtr story_puppet_master_;
  fuchsia::modular::StoryControllerPtr story_controller_;
  fuchsia::modular::LinkPtr session_shell_link_;
  fuchsia::modular::StoryInfo story_info_;

  FXL_DISALLOW_COPY_AND_ASSIGN(TestApp);
};

}  // namespace

int main(int argc, const char** argv) {
  auto command_line = fxl::CommandLineFromArgcArgv(argc, argv);
  modular::testing::ComponentMain<TestApp>();
  return 0;
}
