// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <trace-provider/provider.h>
#include <trace/event.h>
#include <trace/observer.h>
#include <utility>

#include "lib/app/cpp/application_context.h"
#include "lib/app/fidl/service_provider.fidl.h"
#include "lib/fidl/cpp/bindings/binding.h"
#include "lib/fxl/command_line.h"
#include "lib/fxl/logging.h"
#include "lib/fxl/macros.h"
#include "lib/fxl/strings/string_number_conversions.h"
#include "lib/fxl/tasks/task_runner.h"
#include "lib/story/fidl/link.fidl.h"
#include "lib/user/fidl/user_shell.fidl.h"
#include "peridot/lib/fidl/single_service_app.h"
#include "peridot/lib/testing/component_base.h"
#include "peridot/lib/testing/reporting.h"
#include "peridot/lib/testing/testing.h"

namespace {

class Settings {
 public:
  explicit Settings(const fxl::CommandLine& command_line) {
    auto story_count_str = command_line.GetOptionValueWithDefault(
        "story_count", "1");
    if (!fxl::StringToNumberWithError(story_count_str, &story_count)) {
      FXL_LOG(ERROR) << "Unrecognized value [--story_count=" << story_count_str
                     << "]: Using 0.";
    }

    module_url = command_line.GetOptionValueWithDefault(
        "module_url", "file:///system/test/modular_tests/null_module");
  }

  int story_count{0};
  std::string module_url;
};

// A simple story watcher implementation that invokes a "continue" callback when
// it sees the watched story transition to the given state. Used to push the
// test sequence forward when the test story reaches the next state.
class StoryWatcherImpl : modular::StoryWatcher {
 public:
  StoryWatcherImpl() : binding_(this) {}
  ~StoryWatcherImpl() override = default;

  // Registers itself as a watcher on the given story. Only one story at a time
  // can be watched.
  void Watch(modular::StoryControllerPtr* const story_controller) {
    (*story_controller)->Watch(binding_.NewBinding());
  }

  // Deregisters itself from the watched story.
  void Reset() { binding_.Close(); }

  // Sets the function where to continue when the story is observed to be done.
  void Continue(modular::StoryState state, std::function<void()> at) {
    continue_state_ = state;
    continue_ = at;
  }

 private:
  // |StoryWatcher|
  void OnStateChange(modular::StoryState state) override {
    if (state != continue_state_) {
      return;
    }

    continue_();
  }

  // |StoryWatcher|
  void OnModuleAdded(modular::ModuleDataPtr module_data) override {
  }

  fidl::Binding<modular::StoryWatcher> binding_;

  modular::StoryState continue_state_{modular::StoryState::DONE};
  std::function<void()> continue_{[]{}};

  FXL_DISALLOW_COPY_AND_ASSIGN(StoryWatcherImpl);
};

// Measures timing the machinery available to a user shell implementation. This
// is invoked as a user shell from device runner and executes a predefined
// sequence of steps, rather than to expose a UI to be driven by user
// interaction, as a user shell normally would.
class TestApp : public modular::SingleServiceApp<modular::UserShell> {
 public:
  using Base = modular::SingleServiceApp<modular::UserShell>;
  TestApp(app::ApplicationContext* const application_context,
          Settings settings)
      : Base(application_context), settings_(std::move(settings)) {}

  ~TestApp() override = default;

  // Called by AppDriver in ComponentMain(). NOTE(mesch): Even though it
  // overrides SingleServiceApp::Terminate(), it is called directly on TestApp
  // by AppDriver, so it must not be private.
  void Terminate(std::function<void()> done) override {
    // The corresponding BEGIN() call is in Loop(), below.
    TRACE_ASYNC_END("benchmark", "user/logout", 0);
    done();
  }

 private:
  // |UserShell|
  void Initialize(fidl::InterfaceHandle<modular::UserShellContext>
                      user_shell_context) override {
    user_shell_context_.Bind(std::move(user_shell_context));
    user_shell_context_->GetStoryProvider(story_provider_.NewRequest());
    WaitForTracing();
  }

  void WaitForTracing() {
    auto* const loop = fsl::MessageLoop::GetCurrent();

    // Cf. RunWithTracing() used by ledger benchmarks.
    trace_provider_ = std::make_unique<trace::TraceProvider>(loop->async());
    trace_observer_ = std::make_unique<trace::TraceObserver>();

    std::function<void()> on_trace_state_changed = [this] {
      if (TRACE_CATEGORY_ENABLED("benchmark") && !started_) {
        started_ = true;
        Loop();
      }
    };

    // In case tracing has already started.
    on_trace_state_changed();

    if (!started_) {
      trace_observer_->Start(loop->async(), on_trace_state_changed);
    }
  }

  void Loop() {
    if (story_count_ < settings_.story_count) {
      FXL_LOG(INFO) << "Loop at " << story_count_
                    << " of " << settings_.story_count;
      fsl::MessageLoop::GetCurrent()->task_runner()->PostTask(
          [this] { StoryCreate(); });

    } else {
      TRACE_ASYNC_BEGIN("benchmark", "user/logout", 0);
      user_shell_context_->Logout();
    }
  }

  void StoryCreate() {
    TRACE_ASYNC_BEGIN("benchmark", "story/create", 0);
    story_provider_->CreateStory(settings_.module_url,
                                 [this](const fidl::String& story_id) {
                                   TRACE_ASYNC_END("benchmark", "story/create", 0);
                                   StoryInfo(story_id);
                                 });
  }

  void StoryInfo(const fidl::String& story_id) {
    story_provider_->GetController(story_id, story_controller_.NewRequest());

    TRACE_ASYNC_BEGIN("benchmark", "story/info", 0);
    story_controller_->GetInfo(
        [this](modular::StoryInfoPtr story_info, modular::StoryState state) {
          TRACE_ASYNC_END("benchmark", "story/info", 0);
          StoryStart();
        });
  }

  void StoryStart() {
    TRACE_ASYNC_BEGIN("benchmark", "story/start", 0);
    story_watcher_.Continue(modular::StoryState::RUNNING, [this] {
        TRACE_ASYNC_END("benchmark", "story/start", 0);
        StoryStop();
      });

    story_watcher_.Watch(&story_controller_);

    fidl::InterfaceHandle<mozart::ViewOwner> story_view;
    story_controller_->Start(story_view.NewRequest());
  }

  void StoryStop() {
    TRACE_ASYNC_BEGIN("benchmark", "story/stop", 0);
    story_controller_->Stop([this] {
        TRACE_ASYNC_END("benchmark", "story/stop", 0);
        MaybeRepeat();
      });
  }

  void MaybeRepeat() {
    story_watcher_.Reset();
    story_controller_.reset();

    story_count_++;
    Loop();
  }

  const Settings settings_;

  bool started_{};
  std::unique_ptr<trace::TraceProvider> trace_provider_;
  std::unique_ptr<trace::TraceObserver> trace_observer_;

  int story_count_{};

  StoryWatcherImpl story_watcher_;

  modular::UserShellContextPtr user_shell_context_;
  modular::StoryProviderPtr story_provider_;
  modular::StoryControllerPtr story_controller_;

  FXL_DISALLOW_COPY_AND_ASSIGN(TestApp);
};

}  // namespace

int main(int argc, const char** argv) {
  auto command_line = fxl::CommandLineFromArgcArgv(argc, argv);
  Settings settings(command_line);
  modular::testing::ComponentMain<TestApp, Settings>(std::move(settings));
  return 0;
}
