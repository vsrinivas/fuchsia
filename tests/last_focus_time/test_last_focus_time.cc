// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <string>

#include "application/lib/app/connect.h"
#include "application/services/service_provider.fidl.h"
#include "apps/modular/lib/fidl/array_to_string.h"
#include "apps/modular/lib/fidl/single_service_view_app.h"
#include "apps/modular/lib/fidl/view_host.h"
#include "apps/modular/lib/testing/component_base.h"
#include "apps/modular/lib/testing/reporting.h"
#include "apps/modular/lib/testing/testing.h"
#include "apps/modular/services/user/focus.fidl.h"
#include "apps/modular/services/user/user_context.fidl.h"
#include "apps/modular/services/user/user_shell.fidl.h"
#include "apps/mozart/lib/view_framework/base_view.h"
#include "apps/mozart/services/views/view_manager.fidl.h"
#include "apps/mozart/services/views/view_provider.fidl.h"
#include "apps/test_runner/services/test_runner.fidl.h"
#include "lib/fidl/cpp/bindings/binding.h"
#include "lib/ftl/command_line.h"
#include "lib/ftl/functional/make_copyable.h"
#include "lib/ftl/logging.h"
#include "lib/ftl/macros.h"
#include "lib/ftl/tasks/task_runner.h"
#include "lib/ftl/time/time_delta.h"
#include "lib/mtl/tasks/message_loop.h"

namespace {

constexpr char kModuleUrl[] = "file:///system/apps/example_flutter_hello_world";

// A simple story provider watcher implementation. It confirms that it sees an
// increase in the last_focus_time in the StoryInfo it receives, and pushes the
// test through to the next step.
class StoryProviderWatcherImpl : modular::StoryProviderWatcher {
 public:
  StoryProviderWatcherImpl() : binding_(this) {}
  ~StoryProviderWatcherImpl() override = default;

  // Registers itself a watcher on the given story provider. Only one story
  // provider can be watched at a time.
  void Watch(modular::StoryProviderPtr* const story_provider) {
    (*story_provider)->Watch(binding_.NewBinding());
  }

  // Deregisters itself from the watched story provider.
  void Reset() { binding_.Close(); }

  // Sets the function where to continue when the story is observed to be
  // running.
  void Continue(std::function<void()> at) { continue_ = at; }

 private:
  // |StoryProviderWatcher|
  void OnDelete(const ::fidl::String& story_id) override {
    FTL_LOG(INFO) << "TestApp::OnDelete() " << story_id;
  }

  using TestPoint = modular::testing::TestPoint;

  TestPoint last_focus_time_increased_{
      "StoryProviderWatcher::OnChange() last_focus_time increased"};

  // |StoryProviderWatcher|
  void OnChange(modular::StoryInfoPtr story_info,
                modular::StoryState story_state) override {
    FTL_LOG(INFO) << "TestApp::OnChange() " << story_state << " id "
                  << story_info->id << " url " << story_info->url << " focus "
                  << story_info->last_focus_time;

    // We test that we at least see one increase in the last focus time.
    bool cont{};
    if (story_info->last_focus_time > last_focus_time_) {
      // Every time we see an increase in last_focus_time, we push the test
      // sequence forward.
      cont = true;

      // We expect two last_focus_time transitions:
      //
      // 0 -> X on creation of the story.
      // X -> Y where Y > X on focusing the story.
      if (++change_count_ == 2) {
        last_focus_time_increased_.Pass();
      }
    }

    last_focus_time_ = story_info->last_focus_time;

    if (cont) {
      continue_();
    }
  }

  int change_count_{};
  int64_t last_focus_time_{};

  fidl::Binding<modular::StoryProviderWatcher> binding_;
  std::function<void()> continue_;
  FTL_DISALLOW_COPY_AND_ASSIGN(StoryProviderWatcherImpl);
};

class StoryWatcherImpl : modular::StoryWatcher {
 public:
  StoryWatcherImpl() : binding_(this) {}
  ~StoryWatcherImpl() override = default;

  // Registers itself as a watcher on the given story. Only one story at a time
  // can be watched.
  void Watch(modular::StoryController* const story_controller) {
    story_controller->Watch(binding_.NewBinding());
  }

  // Deregisters itself from the watched story.
  void Reset() { binding_.Close(); }

  // Sets the function where to continue when the story is observed to be
  // running.
  void Continue(std::function<void()> at) { continue_ = at; }

 private:
  // |StoryWatcher|
  void OnStateChange(modular::StoryState state) override {
    FTL_LOG(INFO) << "OnStateChange() " << state;
    if (state != modular::StoryState::RUNNING) {
      return;
    }

    continue_();
  }

  // |StoryWatcher|
  void OnModuleAdded(modular::ModuleDataPtr module_data) override {}

  fidl::Binding<modular::StoryWatcher> binding_;
  std::function<void()> continue_;
  FTL_DISALLOW_COPY_AND_ASSIGN(StoryWatcherImpl);
};

// A simple focus watcher implementation that invokes a "continue" callback when
// it sees the next focus change.
class FocusWatcherImpl : modular::FocusWatcher {
 public:
  FocusWatcherImpl() : binding_(this) {}
  ~FocusWatcherImpl() override = default;

  // Registers itself as a watcher on the focus provider.
  void Watch(modular::FocusProvider* const focus_provider) {
    focus_provider->Watch(binding_.NewBinding());
  }

  // Deregisters itself from the watched focus provider.
  void Reset() { binding_.Close(); }

 private:
  // |FocusWatcher|
  void OnFocusChange(modular::FocusInfoPtr info) override {
    FTL_LOG(INFO) << "OnFocusChange() " << info->focused_story_id;
  }

  fidl::Binding<modular::FocusWatcher> binding_;
  FTL_DISALLOW_COPY_AND_ASSIGN(FocusWatcherImpl);
};

// Tests the last_focus_time entry in StoryInfo.
class TestApp : modular::testing::ComponentViewBase<modular::UserShell> {
 public:
  // The app instance must be dynamic, because it needs to do several things
  // after its own constructor is invoked. It accomplishes that by being able to
  // call delete this. Cf. Terminate().
  static void New() {
    new TestApp;  // will delete itself in Terminate().
  }

 private:
  TestApp() { TestInit(__FILE__); }

  ~TestApp() override = default;

  using TestPoint = modular::testing::TestPoint;

  TestPoint create_view_{"CreateView()"};

  // |SingleServiceViewApp|
  void CreateView(
      fidl::InterfaceRequest<mozart::ViewOwner> view_owner_request,
      fidl::InterfaceRequest<app::ServiceProvider> services) override {
    create_view_.Pass();
    view_.reset(new modular::ViewHost(
        application_context()
            ->ConnectToEnvironmentService<mozart::ViewManager>(),
        std::move(view_owner_request)));
  }

  TestPoint initialize_{"Initialize()"};

  // |UserShell|
  void Initialize(fidl::InterfaceHandle<modular::UserContext> user_context,
                  fidl::InterfaceHandle<modular::UserShellContext>
                      user_shell_context) override {
    initialize_.Pass();

    user_context_.Bind(std::move(user_context));
    user_shell_context_.Bind(std::move(user_shell_context));

    user_shell_context_->GetStoryProvider(story_provider_.NewRequest());
    story_provider_watcher_.Watch(&story_provider_);
    story_provider_watcher_.Continue([] {});

    user_shell_context_->GetFocusController(focus_controller_.NewRequest());
    user_shell_context_->GetFocusProvider(focus_provider_.NewRequest());
    focus_watcher_.Watch(focus_provider_.get());

    CreateStory();
  }

  TestPoint create_story_{"CreateStory()"};

  void CreateStory() {
    story_provider_->CreateStory(kModuleUrl,
                                 [this](const fidl::String& story_id) {
                                   create_story_.Pass();
                                   story_id_ = story_id;
                                   StartStory();
                                 });
  }

  TestPoint start_story_{"StartStory()"};

  void StartStory() {
    story_provider_->GetController(story_id_, story_controller_.NewRequest());
    story_watcher_.Watch(story_controller_.get());

    // Start and show the new story.
    fidl::InterfaceHandle<mozart::ViewOwner> story_view;
    story_controller_->Start(story_view.NewRequest());
    view_->ConnectView(std::move(story_view));

    story_watcher_.Continue([this] {
      start_story_.Pass();
      Focus();
    });
  }

  TestPoint focus_{"Focus()"};

  void Focus() {
    focus_controller_->Set(story_id_);

    story_provider_watcher_.Continue([this] {
      focus_.Pass();
      Logout();
    });
  }

  void Logout() {
    story_provider_watcher_.Reset();
    user_context_->Logout();
  }

  TestPoint terminate_{"Terminate()"};

  // |UserShell|
  void Terminate(const TerminateCallback& done) override {
    terminate_.Pass();
    DeleteAndQuit(done);
  }

  std::unique_ptr<modular::ViewHost> view_;

  modular::UserContextPtr user_context_;
  modular::UserShellContextPtr user_shell_context_;

  modular::StoryProviderPtr story_provider_;
  StoryProviderWatcherImpl story_provider_watcher_;

  fidl::String story_id_;
  modular::StoryControllerPtr story_controller_;
  StoryWatcherImpl story_watcher_;

  modular::FocusControllerPtr focus_controller_;
  modular::FocusProviderPtr focus_provider_;
  FocusWatcherImpl focus_watcher_;

  FTL_DISALLOW_COPY_AND_ASSIGN(TestApp);
};

}  // namespace

int main(int argc, const char** argv) {
  mtl::MessageLoop loop;
  TestApp::New();
  loop.Run();
  return 0;
}
