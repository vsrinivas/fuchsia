// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <utility>

#include <fuchsia/modular/cpp/fidl.h>
#include <fuchsia/sys/cpp/fidl.h>
#include <fuchsia/ui/views_v1_token/cpp/fidl.h>
#include <lib/app/cpp/startup_context.h>
#include <lib/async/cpp/task.h>
#include <lib/async/default.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/fxl/command_line.h>
#include <lib/fxl/logging.h>
#include <lib/fxl/macros.h>
#include <lib/fxl/strings/string_number_conversions.h>
#include <trace-provider/provider.h>
#include <trace/event.h>
#include <trace/observer.h>

#include "peridot/lib/common/names.h"
#include "peridot/lib/fidl/single_service_app.h"
#include "peridot/lib/testing/component_base.h"
#include "peridot/lib/testing/reporting.h"
#include "peridot/lib/testing/testing.h"
#include "peridot/tests/benchmark/story/tracing_waiter.h"

namespace {

class Settings {
 public:
  explicit Settings(const fxl::CommandLine& command_line) {
    const auto story_count_str =
        command_line.GetOptionValueWithDefault("story_count", "1");
    if (!fxl::StringToNumberWithError(story_count_str, &story_count)) {
      FXL_LOG(ERROR) << "Unrecognized value [--story_count=" << story_count_str
                     << "]: Using 0.";
    }

    module_url = command_line.GetOptionValueWithDefault(
        "module_url", "modular_benchmark_story_module");
  }

  int story_count{0};
  std::string module_url;
};

// A simple story watcher implementation that invokes a "continue" callback when
// it sees the watched story transition to the given state. Used to push the
// test sequence forward when the test story reaches the next state.
class StoryWatcherImpl : fuchsia::modular::StoryWatcher {
 public:
  StoryWatcherImpl() : binding_(this) {}
  ~StoryWatcherImpl() override = default;

  // Registers itself as a watcher on the given story. Only one story at a time
  // can be watched.
  void Watch(fuchsia::modular::StoryControllerPtr* const story_controller) {
    (*story_controller)->Watch(binding_.NewBinding());
  }

  // Deregisters itself from the watched story.
  void Reset() { binding_.Unbind(); }

  // Sets the function where to continue when the story is observed to be done.
  void Continue(fuchsia::modular::StoryState state, std::function<void()> at) {
    continue_state_ = state;
    continue_ = at;
  }

 private:
  // |fuchsia::modular::StoryWatcher|
  void OnStateChange(fuchsia::modular::StoryState state) override {
    if (state != continue_state_) {
      return;
    }

    continue_();
  }

  // |fuchsia::modular::StoryWatcher|
  void OnModuleAdded(fuchsia::modular::ModuleData module_data) override {}

  fidl::Binding<fuchsia::modular::StoryWatcher> binding_;

  fuchsia::modular::StoryState continue_state_{
      fuchsia::modular::StoryState::STOPPED};
  std::function<void()> continue_{[] {}};

  FXL_DISALLOW_COPY_AND_ASSIGN(StoryWatcherImpl);
};

// A simple link watcher implementation that invokes a "continue" callback when
// it sees the watched link change.
class LinkWatcherImpl : fuchsia::modular::LinkWatcher {
 public:
  LinkWatcherImpl() : binding_(this) {}
  ~LinkWatcherImpl() override = default;

  // Registers itself as a watcher on the given link. Only one story at a time
  // can be watched.
  void Watch(fuchsia::modular::LinkPtr* const link) {
    (*link)->WatchAll(binding_.NewBinding());
  }

  // Deregisters itself from the watched story.
  void Reset() { binding_.Unbind(); }

  // Sets the function where to continue when the story is observed to be done.
  void Continue(std::function<void(fidl::StringPtr)> at) { continue_ = at; }

 private:
  // |fuchsia::modular::LinkWatcher|
  void Notify(fidl::StringPtr json) override { continue_(json); }

  fidl::Binding<fuchsia::modular::LinkWatcher> binding_;

  std::function<void(fidl::StringPtr)> continue_{[](fidl::StringPtr) {}};

  FXL_DISALLOW_COPY_AND_ASSIGN(LinkWatcherImpl);
};

// Measures timing the machinery available to a user shell implementation. This
// is invoked as a user shell from device runner and executes a predefined
// sequence of steps, rather than to expose a UI to be driven by user
// interaction, as a user shell normally would.
class TestApp : public modular::SingleServiceApp<fuchsia::modular::UserShell> {
 public:
  using Base = modular::SingleServiceApp<fuchsia::modular::UserShell>;
  TestApp(fuchsia::sys::StartupContext* const startup_context,
          Settings settings)
      : Base(startup_context), settings_(std::move(settings)) {}

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
  // |fuchsia::modular::UserShell|
  void Initialize(fidl::InterfaceHandle<fuchsia::modular::UserShellContext>
                      user_shell_context) override {
    user_shell_context_.Bind(std::move(user_shell_context));
    user_shell_context_->GetStoryProvider(story_provider_.NewRequest());
    tracing_waiter_.WaitForTracing([this] { Loop(); });
  }

  void Loop() {
    if (story_count_ < settings_.story_count) {
      FXL_LOG(INFO) << "Loop at " << story_count_ << " of "
                    << settings_.story_count;
      async::PostTask(async_get_default(), [this] { StoryCreate(); });

    } else {
      TRACE_ASYNC_BEGIN("benchmark", "user/logout", 0);
      user_shell_context_->Logout();
    }
  }

  void StoryCreate() {
    FXL_LOG(INFO) << "StoryCreate()";
    TRACE_ASYNC_BEGIN("benchmark", "story/create", 0);
    story_provider_->CreateStory(
        settings_.module_url, [this](fidl::StringPtr story_id) {
          TRACE_ASYNC_END("benchmark", "story/create", 0);
          StoryInfo(story_id);
        });
  }

  void StoryInfo(fidl::StringPtr story_id) {
    FXL_LOG(INFO) << "StoryInfo()";
    story_provider_->GetController(story_id, story_controller_.NewRequest());

    TRACE_ASYNC_BEGIN("benchmark", "story/info", 0);
    story_controller_->GetInfo([this](fuchsia::modular::StoryInfo story_info,
                                      fuchsia::modular::StoryState state) {
      TRACE_ASYNC_END("benchmark", "story/info", 0);
      Link();
    });
  }

  void Link() {
    FXL_LOG(INFO) << "Link()";

    fuchsia::modular::LinkPath link_path = fuchsia::modular::LinkPath();
    fidl::VectorPtr<fidl::StringPtr> root_module_path;
    root_module_path.push_back(modular::kRootModuleName);
    link_path.module_path = std::move(root_module_path);
    link_path.link_name = nullptr;
    story_controller_->GetLink(std::move(link_path), link_.NewRequest());

    link_watcher_.Watch(&link_);
    link_watcher_.Continue([this](fidl::StringPtr json) {
      FXL_LOG(INFO) << "Link() Watch: " << json;
      if (json == "") {
        return;
      }

      const int count = fxl::StringToNumber<int>(json.get());

      // Corresponding TRACE_FLOW_BEGIN() is in the module.
      TRACE_FLOW_END("benchmark", "link/trans", count);

      if (count == 100) {
        StoryStop();
      }
    });

    StoryStart();
  }

  void StoryStart() {
    FXL_LOG(INFO) << "StoryStart()";
    TRACE_ASYNC_BEGIN("benchmark", "story/start", 0);
    story_watcher_.Continue(fuchsia::modular::StoryState::RUNNING, [this] {
      TRACE_ASYNC_END("benchmark", "story/start", 0);
    });

    story_watcher_.Watch(&story_controller_);

    fidl::InterfaceHandle<fuchsia::ui::views_v1_token::ViewOwner> story_view;
    story_controller_->Start(story_view.NewRequest());
  }

  void StoryStop() {
    FXL_LOG(INFO) << "StoryStop()";
    TRACE_ASYNC_BEGIN("benchmark", "story/stop", 0);
    story_controller_->Stop([this] {
      TRACE_ASYNC_END("benchmark", "story/stop", 0);
      MaybeRepeat();
    });
  }

  void MaybeRepeat() {
    FXL_LOG(INFO) << "MaybeRepeat()";
    story_watcher_.Reset();
    story_controller_.Unbind();

    story_count_++;
    Loop();
  }

  modular::TracingWaiter tracing_waiter_;

  const Settings settings_;

  int story_count_{};

  StoryWatcherImpl story_watcher_;
  LinkWatcherImpl link_watcher_;

  fuchsia::modular::UserShellContextPtr user_shell_context_;
  fuchsia::modular::StoryProviderPtr story_provider_;
  fuchsia::modular::StoryControllerPtr story_controller_;
  fuchsia::modular::LinkPtr link_;

  FXL_DISALLOW_COPY_AND_ASSIGN(TestApp);
};

}  // namespace

int main(int argc, const char** argv) {
  auto command_line = fxl::CommandLineFromArgcArgv(argc, argv);
  Settings settings(command_line);
  modular::testing::ComponentMain<TestApp, Settings>(std::move(settings));
  return 0;
}
