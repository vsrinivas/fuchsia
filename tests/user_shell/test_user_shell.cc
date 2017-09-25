// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <utility>

#include "lib/app/fidl/service_provider.fidl.h"
#include "lib/fidl/cpp/bindings/binding.h"
#include "lib/fsl/tasks/message_loop.h"
#include "lib/fxl/command_line.h"
#include "lib/fxl/logging.h"
#include "lib/fxl/macros.h"
#include "lib/fxl/tasks/task_runner.h"
#include "lib/fxl/time/time_delta.h"
#include "lib/story/fidl/link.fidl.h"
#include "lib/suggestion/fidl/suggestion_provider.fidl.h"
#include "lib/ui/views/fidl/view_manager.fidl.h"
#include "lib/ui/views/fidl/view_provider.fidl.h"
#include "lib/user/fidl/user_shell.fidl.h"
#include "peridot/lib/fidl/single_service_app.h"
#include "peridot/lib/rapidjson/rapidjson.h"
#include "peridot/lib/testing/component_base.h"
#include "peridot/lib/testing/reporting.h"
#include "peridot/lib/testing/testing.h"

namespace {

class Settings {
 public:
  explicit Settings(const fxl::CommandLine& command_line) {
    first_module = command_line.GetOptionValueWithDefault(
        "first_module", "file:///system/apps/modular_tests/null_module");
    second_module = command_line.GetOptionValueWithDefault(
        "second_module", "file:///system/apps/modular_tests/null_module");
  }

  std::string first_module;
  std::string second_module;
};

// A simple story watcher implementation that invokes a "continue" callback when
// it sees the watched story transition to DONE state. Used to push the test
// sequence forward when the test story is done.
class StoryDoneWatcherImpl : modular::StoryWatcher {
 public:
  StoryDoneWatcherImpl() : binding_(this) {}
  ~StoryDoneWatcherImpl() override = default;

  // Registers itself as a watcher on the given story. Only one story at a time
  // can be watched.
  void Watch(modular::StoryControllerPtr* const story_controller) {
    (*story_controller)->Watch(binding_.NewBinding());
  }

  // Deregisters itself from the watched story.
  void Reset() { binding_.Close(); }

  // Sets the function where to continue when the story is observed to be done.
  void Continue(std::function<void()> at) { continue_ = at; }

 private:
  // |StoryWatcher|
  void OnStateChange(modular::StoryState state) override {
    if (state != modular::StoryState::DONE) {
      return;
    }

    continue_();
  }

  // |StoryWatcher|
  void OnModuleAdded(modular::ModuleDataPtr module_data) override {
    FXL_LOG(INFO) << "OnModuleAdded: " << module_data->module_url;
    if (!on_module_added_called_) {
      on_module_added_.Pass();
      on_module_added_called_ = true;
    }
  }

  fidl::Binding<modular::StoryWatcher> binding_;
  std::function<void()> continue_;
  modular::testing::TestPoint on_module_added_{"OnModuleAdded"};
  bool on_module_added_called_ = false;
  FXL_DISALLOW_COPY_AND_ASSIGN(StoryDoneWatcherImpl);
};

// A simple story modules watcher implementation that just logs the
// notifications it receives.
class StoryModulesWatcherImpl : modular::StoryModulesWatcher {
 public:
  StoryModulesWatcherImpl() : binding_(this) {}
  ~StoryModulesWatcherImpl() override = default;

  // Registers itself as a watcher on the given story. Only one story at a time
  // can be watched.
  void Watch(modular::StoryControllerPtr* const story_controller) {
    (*story_controller)
        ->GetActiveModules(binding_.NewBinding(),
                           [this](fidl::Array<modular::ModuleDataPtr> data) {
                             FXL_LOG(INFO)
                                 << "StoryModulesWatcherImpl GetModules(): "
                                 << data.size() << " modules";
                           });
  }

  // Deregisters itself from the watched story.
  void Reset() { binding_.Close(); }

 private:
  // |StoryModulesWatcher|
  void OnNewModule(modular::ModuleDataPtr data) override {
    FXL_LOG(INFO) << "New Module: " << data->module_url;
  }

  // |StoryModulesWatcher|
  void OnStopModule(modular::ModuleDataPtr data) override {
    FXL_LOG(INFO) << "Stop Module: " << data->module_url;
  }

  fidl::Binding<modular::StoryModulesWatcher> binding_;
  FXL_DISALLOW_COPY_AND_ASSIGN(StoryModulesWatcherImpl);
};

// A simple story links watcher implementation that just logs the notifications
// it receives.
class StoryLinksWatcherImpl : modular::StoryLinksWatcher {
 public:
  StoryLinksWatcherImpl() : binding_(this) {}
  ~StoryLinksWatcherImpl() override = default;

  // Registers itself as a watcher on the given story. Only one story at a time
  // can be watched.
  void Watch(modular::StoryControllerPtr* const story_controller) {
    (*story_controller)
        ->GetActiveLinks(binding_.NewBinding(),
                         [this](fidl::Array<modular::LinkPathPtr> data) {
                           FXL_LOG(INFO) << "StoryLinksWatcherImpl GetLinks(): "
                                         << data.size() << " links";
                         });
  }

  // Deregisters itself from the watched story.
  void Reset() { binding_.Close(); }

 private:
  // |StoryLinksWatcher|
  void OnNewLink(modular::LinkPathPtr data) override {
    FXL_LOG(INFO) << "New Link: " << data->link_name;
  }

  fidl::Binding<modular::StoryLinksWatcher> binding_;
  FXL_DISALLOW_COPY_AND_ASSIGN(StoryLinksWatcherImpl);
};

// A simple story provider watcher implementation. Just logs observed state
// transitions.
class StoryProviderStateWatcherImpl : modular::StoryProviderWatcher {
 public:
  StoryProviderStateWatcherImpl() : binding_(this) {}
  ~StoryProviderStateWatcherImpl() override = default;

  // Registers itself a watcher on the given story provider. Only one story
  // provider can be watched at a time.
  void Watch(modular::StoryProviderPtr* const story_provider) {
    (*story_provider)->Watch(binding_.NewBinding());
  }

  // Deregisters itself from the watched story provider.
  void Reset() { binding_.Close(); }

 private:
  modular::testing::TestPoint on_delete_called_once_{"OnDelete() Called"};
  int on_delete_called_{};

  // |StoryProviderWatcher|
  void OnDelete(const fidl::String& story_id) override {
    FXL_LOG(INFO) << "StoryProviderStateWatcherImpl::OnDelete() " << story_id;

    if (++on_delete_called_ == 1) {
      on_delete_called_once_.Pass();
    }

    deleted_stories_.emplace(story_id);
  }

  modular::testing::TestPoint on_starting_called_once_{
      "OnChange() STARTING Called"};
  int on_starting_called_{};

  modular::testing::TestPoint on_running_called_once_{
      "OnChange() RUNNING Called"};
  int on_running_called_{};

  modular::testing::TestPoint on_stopped_called_once_{
      "OnChange() STOPPED Called"};
  int on_stopped_called_{};

  modular::testing::TestPoint on_done_called_once_{"OnChange() DONE Called"};
  int on_done_called_{};

  // |StoryProviderWatcher|
  void OnChange(const modular::StoryInfoPtr story_info,
                const modular::StoryState story_state) override {
    FXL_LOG(INFO) << "StoryProviderStateWatcherImpl::OnChange() "
                  << " id " << story_info->id << " state " << story_state
                  << " url " << story_info->url;

    if (deleted_stories_.find(story_info->id) != deleted_stories_.end()) {
      FXL_LOG(ERROR) << "Status change notification for deleted story "
                     << story_info->id;
      modular::testing::Fail("Status change notification for deleted story");
    }

    // Just check that all states are covered at least once, proving that we get
    // state notifications at all from the story provider.
    switch (story_state) {
      case modular::StoryState::INITIAL:
        FXL_CHECK(story_state != modular::StoryState::INITIAL);
        // Doesn't happen in this test, presumably because of the STOPPED
        // StoryState HACK(jimbe) in StoryProviderImpl::OnChange().
        break;
      case modular::StoryState::STARTING:
        if (++on_starting_called_ == 1) {
          on_starting_called_once_.Pass();
        }
        break;
      case modular::StoryState::RUNNING:
        if (++on_running_called_ == 1) {
          on_running_called_once_.Pass();
        }
        break;
      case modular::StoryState::STOPPED:
        if (++on_stopped_called_ == 1) {
          on_stopped_called_once_.Pass();
        }
        break;
      case modular::StoryState::DONE:
        if (++on_done_called_ == 1) {
          on_done_called_once_.Pass();
        }
        break;
      case modular::StoryState::ERROR:
        // Doesn't happen in this test.
        FXL_CHECK(story_state != modular::StoryState::ERROR);
        break;
    }
  }

  fidl::Binding<modular::StoryProviderWatcher> binding_;

  // Remember deleted stories. After a story is deleted, there must be no state
  // change notifications for it.
  std::set<std::string> deleted_stories_;

  FXL_DISALLOW_COPY_AND_ASSIGN(StoryProviderStateWatcherImpl);
};

// Tests the machinery available to a user shell implementation. This is invoked
// as a user shell from device runner and executes a predefined sequence of
// steps, rather than to expose a UI to be driven by user interaction, as a user
// shell normally would.
class TestUserShellApp : modular::testing::ComponentBase<modular::UserShell> {
 public:
  // The app instance must be dynamic, because it needs to do several things
  // after its own constructor is invoked. It accomplishes that by being able to
  // call delete this. Cf. Terminate().
  static void New(const Settings& settings) {
    new TestUserShellApp(settings);  // will delete itself in Terminate().
  }

 private:
  explicit TestUserShellApp(Settings settings)
      : settings_(std::move(settings)) {
    TestInit(__FILE__);
  }

  ~TestUserShellApp() override = default;

  using TestPoint = modular::testing::TestPoint;

  TestPoint create_view_{"CreateView()"};

  // |SingleServiceApp|
  void CreateView(
      fidl::InterfaceRequest<mozart::ViewOwner> /*view_owner_request*/,
      fidl::InterfaceRequest<app::ServiceProvider> /*services*/) override {
    create_view_.Pass();
  }

  TestPoint initialize_{"Initialize()"};

  // |UserShell|
  void Initialize(fidl::InterfaceHandle<modular::UserShellContext>
                      user_shell_context) override {
    initialize_.Pass();

    user_shell_context_.Bind(std::move(user_shell_context));
    user_shell_context_->GetStoryProvider(story_provider_.NewRequest());
    story_provider_state_watcher_.Watch(&story_provider_);

    TestStoryProvider_GetStoryInfo_Null();
  }

  TestPoint get_story_info_null_{"StoryProvider.GetStoryInfo() is null"};

  void TestStoryProvider_GetStoryInfo_Null() {
    story_provider_->GetStoryInfo("X",
                                  [this](modular::StoryInfoPtr story_info) {
                                    if (story_info.is_null()) {
                                      get_story_info_null_.Pass();
                                    }

                                    TestUserShellContext_GetLink();
                                  });
  }

  TestPoint get_link_{"UserShellContext.GetLink()"};

  void TestUserShellContext_GetLink() {
    user_shell_context_->GetLink(user_shell_link_.NewRequest());
    user_shell_link_->Get(nullptr, [this](const fidl::String& value) {
      get_link_.Pass();
      TestStoryProvider_PreviousStories();
    });
  }

  TestPoint previous_stories_{"StoryProvider.PreviousStories()"};

  void TestStoryProvider_PreviousStories() {
    story_provider_->PreviousStories([this](fidl::Array<fidl::String> stories) {
      previous_stories_.Pass();
      TestStoryProvider_GetStoryInfo(std::move(stories));
    });
  }

  TestPoint get_story_info_{"StoryProvider.GetStoryInfo()"};

  void TestStoryProvider_GetStoryInfo(fidl::Array<fidl::String> stories) {
    if (stories.empty()) {
      get_story_info_.Pass();
      TestStory1();
      return;
    }

    std::shared_ptr<unsigned int> count = std::make_shared<unsigned int>(0);
    for (auto& story_id : stories) {
      story_provider_->GetStoryInfo(
          story_id, [ this, story_id, count,
                      max = stories.size() ](modular::StoryInfoPtr story_info) {
            ++*count;

            if (!story_info.is_null()) {
              FXL_LOG(INFO) << "Previous story " << *count << " of " << max
                            << " " << story_id << " " << story_info->url;
            } else {
              FXL_LOG(INFO) << "Previous story " << *count << " of " << max
                            << " " << story_id << " was deleted";
            }

            if (*count == max) {
              get_story_info_.Pass();
              TestStory1();
            }
          });
    }
  }

  TestPoint story1_create_{"Story1 Create"};

  void TestStory1() {
    const std::string& url = settings_.first_module;

    modular::JsonDoc doc;
    std::vector<std::string> segments{"example", url, "created-with-info"};
    modular::CreatePointer(doc, segments.begin(), segments.end())
        .Set(doc, true);

    using FidlStringMap = fidl::Map<fidl::String, fidl::String>;
    story_provider_->CreateStoryWithInfo(url, FidlStringMap(),
                                         modular::JsonValueToString(doc),
                                         [this](const fidl::String& story_id) {
                                           story1_create_.Pass();
                                           TestStory1_GetController(story_id);
                                         });
  }

  TestPoint story1_get_controller_{"Story1 GetController"};

  void TestStory1_GetController(const fidl::String& story_id) {
    story_provider_->GetController(story_id, story_controller_.NewRequest());
    story_controller_->GetInfo(
        [this](modular::StoryInfoPtr story_info, modular::StoryState state) {
          story1_get_controller_.Pass();
          story_info_ = std::move(story_info);
          TestStory1_Run();
        });
  }

  TestPoint story1_run_{"Story1 Run"};

  void TestStory1_Run() {
    story_done_watcher_.Continue([this] {
      story_controller_->Stop([this] {
        TeardownStoryController();
        story1_run_.Pass();

        // When the story is done, we start the next one.
        fsl::MessageLoop::GetCurrent()->task_runner()->PostTask(
            [this] { TestStory2(); });
      });
    });

    story_done_watcher_.Watch(&story_controller_);

    story_modules_watcher_.Watch(&story_controller_);
    story_links_watcher_.Watch(&story_controller_);

    // Start and show the new story.
    fidl::InterfaceHandle<mozart::ViewOwner> story_view;
    story_controller_->Start(story_view.NewRequest());
  }

  TestPoint story2_create_{"Story2 Create"};

  void TestStory2() {
    const std::string& url = settings_.second_module;

    modular::JsonDoc doc;
    std::vector<std::string> segments{"example", url, "created-with-info"};
    modular::CreatePointer(doc, segments.begin(), segments.end())
        .Set(doc, true);

    using FidlStringMap = fidl::Map<fidl::String, fidl::String>;
    story_provider_->CreateStoryWithInfo(url, FidlStringMap(),
                                         modular::JsonValueToString(doc),
                                         [this](const fidl::String& story_id) {
                                           story2_create_.Pass();
                                           TestStory2_GetController(story_id);
                                         });
  }

  TestPoint story2_get_controller_{"Story2 Get Controller"};

  void TestStory2_GetController(const fidl::String& story_id) {
    story_provider_->GetController(story_id, story_controller_.NewRequest());
    story_controller_->GetInfo(
        [this](modular::StoryInfoPtr story_info, modular::StoryState state) {
          story_info_ = std::move(story_info);

          story2_get_controller_.Pass();

          TestStory2_GetModules();
        });
  }

  TestPoint story2_get_modules_{"Story2 Get Modules"};

  void TestStory2_GetModules() {
    story_controller_->GetModules(
        [this](fidl::Array<modular::ModuleDataPtr> modules) {
          story2_get_modules_.Pass();

          FXL_LOG(INFO) << "TestUserShell MODULES:";
          for (const auto& module_data : modules) {
            FXL_LOG(INFO) << "TestUserShell MODULE: url="
                          << module_data->module_url;
            FXL_LOG(INFO) << "TestUserShell         link="
                          << module_data->link_path->link_name;
            std::string path;
            for (const auto& path_element : module_data->module_path) {
              path.push_back(' ');
              path.append(path_element);
            }
            if (!path.empty()) {
              FXL_LOG(INFO) << "TestUserShell         path=" << path.substr(1);
            }
          }

          TestStory2_Run();
        });
  }

  TestPoint story2_info_before_run_{"Story2 GetInfo before Run"};
  TestPoint story2_run_{"Story2 Run"};

  void TestStory2_Run() {
    story_controller_->GetInfo(
        [this](modular::StoryInfoPtr info, modular::StoryState state) {
          story2_info_before_run_.Pass();
          FXL_LOG(INFO) << "StoryState before Start(): " << state;

          if (state != modular::StoryState::INITIAL &&
              state != modular::StoryState::STOPPED) {
            modular::testing::Fail(
                "StoryState before Start() must be STARTING or RUNNING.");
          }
        });

    // Start and show the new story.
    fidl::InterfaceHandle<mozart::ViewOwner> story_view;
    story_controller_->Start(story_view.NewRequest());

    story_controller_->GetInfo([this](modular::StoryInfoPtr info,
                                      modular::StoryState state) {
      story2_run_.Pass();

      FXL_LOG(INFO) << "StoryState after Start(): " << state;

      if (state != modular::StoryState::STARTING &&
          state != modular::StoryState::RUNNING) {
        modular::testing::Fail(
            "StoryState after Start() must be STARTING or RUNNING.");
      }

      fsl::MessageLoop::GetCurrent()->task_runner()->PostDelayedTask(
          [this] { TestStory2_DeleteStory(); }, fxl::TimeDelta::FromSeconds(5));
    });
  }

  TestPoint story2_delete_{"Story2 Delete"};

  void TestStory2_DeleteStory() {
    story_provider_->DeleteStory(story_info_->id,
                                 [this] { story2_delete_.Pass(); });

    story_provider_->GetStoryInfo(
        story_info_->id, [this](modular::StoryInfoPtr info) {
          TestStory2_InfoAfterDeleteIsNull(std::move(info));
        });
  }

  TestPoint story2_info_after_delete_{"Story2 Info After Delete"};

  void TestStory2_InfoAfterDeleteIsNull(modular::StoryInfoPtr info) {
    story2_info_after_delete_.Pass();
    if (!info.is_null()) {
      modular::testing::Fail("StoryInfo after DeleteStory() must return null.");
    }

    user_shell_context_->Logout();
  }

  TestPoint terminate_{"Terminate"};

  // |UserShell|
  void Terminate() override {
    terminate_.Pass();
    DeleteAndQuit();
  }

  void TeardownStoryController() {
    story_done_watcher_.Reset();
    story_modules_watcher_.Reset();
    story_links_watcher_.Reset();
    story_controller_.reset();
  }

  const Settings settings_;

  StoryProviderStateWatcherImpl story_provider_state_watcher_;
  StoryDoneWatcherImpl story_done_watcher_;
  StoryModulesWatcherImpl story_modules_watcher_;
  StoryLinksWatcherImpl story_links_watcher_;

  modular::UserShellContextPtr user_shell_context_;
  modular::StoryProviderPtr story_provider_;
  modular::StoryControllerPtr story_controller_;
  modular::LinkPtr user_shell_link_;
  modular::StoryInfoPtr story_info_;

  FXL_DISALLOW_COPY_AND_ASSIGN(TestUserShellApp);
};

}  // namespace

int main(int argc, const char** argv) {
  auto command_line = fxl::CommandLineFromArgcArgv(argc, argv);
  Settings settings(command_line);

  fsl::MessageLoop loop;
  TestUserShellApp::New(settings);
  loop.Run();
  return 0;
}
