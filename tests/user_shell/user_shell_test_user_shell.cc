// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <set>
#include <utility>

#include <fuchsia/modular/cpp/fidl.h>
#include <fuchsia/sys/cpp/fidl.h>
#include <fuchsia/ui/viewsv1token/cpp/fidl.h>
#include <lib/async/cpp/task.h>
#include <lib/async/default.h>
#include <lib/component/cpp/startup_context.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/fxl/command_line.h>
#include <lib/fxl/logging.h>
#include <lib/fxl/macros.h>
#include <lib/fxl/time/time_delta.h>

#include "peridot/lib/rapidjson/rapidjson.h"
#include "peridot/lib/testing/component_base.h"
#include "peridot/lib/testing/reporting.h"
#include "peridot/lib/testing/testing.h"
#include "peridot/tests/common/defs.h"
#include "peridot/tests/user_shell/defs.h"

using modular::testing::Await;
using modular::testing::Signal;
using modular::testing::TestPoint;

namespace {

// A simple story modules watcher implementation that just logs the
// notifications it receives.
class StoryModulesWatcherImpl : fuchsia::modular::StoryModulesWatcher {
 public:
  StoryModulesWatcherImpl() : binding_(this) {}
  ~StoryModulesWatcherImpl() override = default;

  // Registers itself as a watcher on the given story. Only one story at a time
  // can be watched.
  void Watch(fuchsia::modular::StoryControllerPtr* const story_controller) {
    (*story_controller)
        ->GetActiveModules(
            binding_.NewBinding(),
            [this](fidl::VectorPtr<fuchsia::modular::ModuleData> data) {
              FXL_LOG(INFO)
                  << "StoryModulesWatcherImpl GetModules(): " << data->size()
                  << " modules";
            });
  }

  // Deregisters itself from the watched story.
  void Reset() { binding_.Unbind(); }

 private:
  // |fuchsia::modular::StoryModulesWatcher|
  void OnNewModule(fuchsia::modular::ModuleData data) override {
    FXL_LOG(INFO) << "New Module: " << data.module_url;
  }

  // |fuchsia::modular::StoryModulesWatcher|
  void OnStopModule(fuchsia::modular::ModuleData data) override {
    FXL_LOG(INFO) << "Stop Module: " << data.module_url;
  }

  fidl::Binding<fuchsia::modular::StoryModulesWatcher> binding_;
  FXL_DISALLOW_COPY_AND_ASSIGN(StoryModulesWatcherImpl);
};

// A simple story links watcher implementation that just logs the notifications
// it receives.
class StoryLinksWatcherImpl : fuchsia::modular::StoryLinksWatcher {
 public:
  StoryLinksWatcherImpl() : binding_(this) {}
  ~StoryLinksWatcherImpl() override = default;

  // Registers itself as a watcher on the given story. Only one story at a time
  // can be watched.
  void Watch(fuchsia::modular::StoryControllerPtr* const story_controller) {
    (*story_controller)
        ->GetActiveLinks(
            binding_.NewBinding(),
            [this](fidl::VectorPtr<fuchsia::modular::LinkPath> data) {
              FXL_LOG(INFO)
                  << "StoryLinksWatcherImpl GetLinks(): " << data->size()
                  << " links";
            });
  }

  // Deregisters itself from the watched story.
  void Reset() { binding_.Unbind(); }

 private:
  // |fuchsia::modular::StoryLinksWatcher|
  void OnNewLink(fuchsia::modular::LinkPath data) override {
    FXL_LOG(INFO) << "New Link: " << data.link_name;
  }

  fidl::Binding<fuchsia::modular::StoryLinksWatcher> binding_;
  FXL_DISALLOW_COPY_AND_ASSIGN(StoryLinksWatcherImpl);
};

// A simple story provider watcher implementation. Just logs observed state
// transitions.
class StoryProviderStateWatcherImpl : fuchsia::modular::StoryProviderWatcher {
 public:
  StoryProviderStateWatcherImpl() : binding_(this) {}
  ~StoryProviderStateWatcherImpl() override = default;

  // Registers itself a watcher on the given story provider. Only one story
  // provider can be watched at a time.
  void Watch(fuchsia::modular::StoryProviderPtr* const story_provider) {
    (*story_provider)->Watch(binding_.NewBinding());
  }

  // Deregisters itself from the watched story provider.
  void Reset() { binding_.Unbind(); }

 private:
  TestPoint on_delete_called_once_{"OnDelete() Called"};
  int on_delete_called_{};

  // |fuchsia::modular::StoryProviderWatcher|
  void OnDelete(fidl::StringPtr story_id) override {
    FXL_LOG(INFO) << "StoryProviderStateWatcherImpl::OnDelete() " << story_id;

    if (++on_delete_called_ == 1) {
      on_delete_called_once_.Pass();
    }

    deleted_stories_.emplace(story_id);
  }

  TestPoint on_running_called_once_{"OnChange() RUNNING Called"};
  int on_running_called_{};

  TestPoint on_stopped_called_once_{"OnChange() STOPPED Called"};
  int on_stopped_called_{};

  // |fuchsia::modular::StoryProviderWatcher|
  void OnChange(const fuchsia::modular::StoryInfo story_info,
                const fuchsia::modular::StoryState story_state) override {
    FXL_LOG(INFO) << "StoryProviderStateWatcherImpl::OnChange() "
                  << " id " << story_info.id << " state "
                  << fidl::ToUnderlying(story_state) << " url "
                  << story_info.url;

    if (deleted_stories_.find(story_info.id) != deleted_stories_.end()) {
      FXL_LOG(ERROR) << "Status change notification for deleted story "
                     << story_info.id;
      modular::testing::Fail("Status change notification for deleted story");
    }

    // Just check that all states are covered at least once, proving that we get
    // state notifications at all from the story provider.
    switch (story_state) {
      case fuchsia::modular::StoryState::RUNNING:
        if (++on_running_called_ == 1) {
          on_running_called_once_.Pass();
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

  FXL_DISALLOW_COPY_AND_ASSIGN(StoryProviderStateWatcherImpl);
};

// A simple story activity watcher implementation.
class StoryActivityWatcherImpl : fuchsia::modular::StoryActivityWatcher {
 public:
  StoryActivityWatcherImpl() : binding_(this), cont_([](fidl::StringPtr) {}) {}
  ~StoryActivityWatcherImpl() override = default;

  void Watch(fuchsia::modular::StoryProviderPtr* const story_provider) {
    (*story_provider)->WatchActivity(binding_.NewBinding());
  }

  void Continue(std::function<void(fidl::StringPtr)> cont) {
    cont_ = std::move(cont);
  }

  void Reset() { binding_.Unbind(); }

 private:
  // |fuchsia::modular::StoryActivityWatcher|
  void OnStoryActivity(fidl::StringPtr story_id) override {
    cont_(std::move(story_id));
  }

  fidl::Binding<fuchsia::modular::StoryActivityWatcher> binding_;
  std::function<void(fidl::StringPtr)> cont_;

  FXL_DISALLOW_COPY_AND_ASSIGN(StoryActivityWatcherImpl);
};

// Cf. README.md for what this test does and how.
class TestApp
    : public modular::testing::ComponentBase<fuchsia::modular::UserShell> {
 public:
  explicit TestApp(component::StartupContext* const startup_context)
      : ComponentBase(startup_context) {
    TestInit(__FILE__);
  }

  ~TestApp() override = default;

 private:
  TestPoint create_view_{"CreateView()"};

  // |SingleServiceApp|
  void CreateView(
      fidl::InterfaceRequest<
          fuchsia::ui::viewsv1token::ViewOwner> /*view_owner_request*/,
      fidl::InterfaceRequest<fuchsia::sys::ServiceProvider> /*services*/)
      override {
    create_view_.Pass();
  }

  TestPoint initialize_{"Initialize()"};

  // |fuchsia::modular::UserShell|
  void Initialize(fidl::InterfaceHandle<fuchsia::modular::UserShellContext>
                      user_shell_context) override {
    initialize_.Pass();

    user_shell_context_.Bind(std::move(user_shell_context));
    user_shell_context_->GetStoryProvider(story_provider_.NewRequest());
    story_provider_state_watcher_.Watch(&story_provider_);
    story_activity_watcher_.Watch(&story_provider_);

    TestStoryProvider_GetStoryInfo_Null();
  }

  TestPoint get_story_info_null_{"StoryProvider.GetStoryInfo() is null"};

  void TestStoryProvider_GetStoryInfo_Null() {
    story_provider_->GetStoryInfo(
        "X", [this](fuchsia::modular::StoryInfoPtr story_info) {
          if (!story_info) {
            get_story_info_null_.Pass();
          }

          TestUserShellContext_GetLink();
        });
  }

  TestPoint get_link_{"UserShellContext.GetLink()"};

  void TestUserShellContext_GetLink() {
    user_shell_context_->GetLink(user_shell_link_.NewRequest());
    user_shell_link_->Get(nullptr, [this](fidl::StringPtr value) {
      get_link_.Pass();
      TestStoryProvider_PreviousStories();
    });
  }

  TestPoint previous_stories_{"StoryProvider.PreviousStories()"};

  void TestStoryProvider_PreviousStories() {
    story_provider_->PreviousStories(
        [this](fidl::VectorPtr<fuchsia::modular::StoryInfo> stories) {
          previous_stories_.Pass();
          TestStoryProvider_GetStoryInfo(std::move(stories));
        });
  }

  TestPoint get_story_info_{"StoryProvider.GetStoryInfo()"};

  void TestStoryProvider_GetStoryInfo(
      fidl::VectorPtr<fuchsia::modular::StoryInfo> stories) {
    if (stories->empty()) {
      get_story_info_.Pass();
    } else {
      FXL_LOG(ERROR) << "StoryProvider.GetStoryInfo() " << stories->size();
      for (const auto& item : stories.get()) {
        FXL_LOG(INFO) << item.id;
      }
    }

    TestStory1();
  }

  TestPoint story1_create_{"Story1 Create"};

  void TestStory1() {
    const std::string initial_json = R"({"created-with-info": true})";
    story_provider_->CreateStoryWithInfo(kCommonActiveModule,
                                         nullptr /* extra_info */, initial_json,
                                         [this](fidl::StringPtr story_id) {
                                           story1_create_.Pass();
                                           TestStory1_GetController(story_id);
                                         });
  }

  TestPoint story1_get_controller_{"Story1 GetController"};

  void TestStory1_GetController(fidl::StringPtr story_id) {
    story_provider_->GetController(story_id, story_controller_.NewRequest());
    story_controller_->GetInfo([this](fuchsia::modular::StoryInfo story_info,
                                      fuchsia::modular::StoryState state) {
      story1_get_controller_.Pass();
      story_info_ = std::move(story_info);
      TestStory1_Run();
    });
  }

  TestPoint story1_run_{"Story1 Run"};
  TestPoint story1_active_{"Story1 Active"};

  void TestStory1_Run() {
    story_activity_watcher_.Continue([this](fidl::StringPtr story_id) {
      if (story_id == story_info_.id) {
        story1_active_.Pass();
      }

      story_activity_watcher_.Reset();
      TestStory1_Stop();
    });

    story_modules_watcher_.Watch(&story_controller_);
    story_links_watcher_.Watch(&story_controller_);

    // Start and show the new story.
    fidl::InterfaceHandle<fuchsia::ui::viewsv1token::ViewOwner> story_view;
    story_controller_->Start(story_view.NewRequest());
    story1_run_.Pass();
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

  TestPoint story2_create_{"Story2 Create"};

  void TestStory2() {
    const std::string url = kCommonNullModule;
    story_provider_->CreateStory(url, [this](fidl::StringPtr story_id) {
      story2_create_.Pass();
      TestStory2_GetController(story_id);
    });
  }

  TestPoint story2_get_controller_{"Story2 Get Controller"};

  void TestStory2_GetController(fidl::StringPtr story_id) {
    story_provider_->GetController(story_id, story_controller_.NewRequest());
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
        [this](fidl::VectorPtr<fuchsia::modular::ModuleData> modules) {
          if (modules->size() == 1) {
            story2_get_modules_.Pass();
          }

          TestStory2_Run();
        });
  }

  TestPoint story2_state_before_run_{"Story2 State before Run"};
  TestPoint story2_state_after_run_{"Story2 State after Run"};

  void TestStory2_Run() {
    story_controller_->GetInfo([this](fuchsia::modular::StoryInfo info,
                                      fuchsia::modular::StoryState state) {
      if (state == fuchsia::modular::StoryState::STOPPED) {
        story2_state_before_run_.Pass();
      }
    });

    // Start and show the new story *while* the GetInfo() call above is in
    // flight.
    fidl::InterfaceHandle<fuchsia::ui::viewsv1token::ViewOwner> story_view;
    story_controller_->Start(story_view.NewRequest());

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
    story_provider_->DeleteStory(story_info_.id,
                                 [this] { story2_delete_.Pass(); });

    story_provider_->GetStoryInfo(
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

    Signal(modular::testing::kTestShutdown);
  }

  void TeardownStoryController() {
    story_modules_watcher_.Reset();
    story_links_watcher_.Reset();
    story_controller_.Unbind();
  }

  StoryProviderStateWatcherImpl story_provider_state_watcher_;
  StoryActivityWatcherImpl story_activity_watcher_;
  StoryModulesWatcherImpl story_modules_watcher_;
  StoryLinksWatcherImpl story_links_watcher_;

  fuchsia::modular::UserShellContextPtr user_shell_context_;
  fuchsia::modular::StoryProviderPtr story_provider_;
  fuchsia::modular::StoryControllerPtr story_controller_;
  fuchsia::modular::LinkPtr user_shell_link_;
  fuchsia::modular::StoryInfo story_info_;

  FXL_DISALLOW_COPY_AND_ASSIGN(TestApp);
};

}  // namespace

int main(int argc, const char** argv) {
  auto command_line = fxl::CommandLineFromArgcArgv(argc, argv);
  modular::testing::ComponentMain<TestApp>();
  return 0;
}
