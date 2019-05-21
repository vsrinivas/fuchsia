// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <utility>

#include <fuchsia/modular/cpp/fidl.h>
#include <fuchsia/sys/cpp/fidl.h>
#include <lib/async/cpp/task.h>
#include <lib/async/default.h>
#include <lib/component/cpp/startup_context.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/fsl/vmo/strings.h>
#include <src/lib/fxl/command_line.h>
#include <src/lib/fxl/logging.h>
#include <src/lib/fxl/macros.h>
#include <src/lib/fxl/strings/string_number_conversions.h>
#include <trace-provider/provider.h>
#include <trace/event.h>
#include <trace/observer.h>

#include "peridot/lib/fidl/single_service_app.h"
#include "peridot/lib/testing/component_main.h"
#include "peridot/lib/testing/session_shell_impl.h"
#include "peridot/public/lib/integration_testing/cpp/reporting.h"
#include "peridot/public/lib/integration_testing/cpp/testing.h"
#include "peridot/tests/benchmarks/story/tracing_waiter.h"

namespace {

const char kStoryNamePrefix[] = "story-";
const char kRootModuleName[] = "root";

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
        "module_url",
        "fuchsia-pkg://fuchsia.com/modular_benchmark_story_module#meta/"
        "modular_benchmark_story_module.cmx");
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
  void Watch(fuchsia::modular::StoryController* const story_controller) {
    story_controller->Watch(binding_.NewBinding());
  }

  // Deregisters itself from the watched story.
  void Reset() { binding_.Unbind(); }

  // Sets the function where to continue when the story is observed to be done.
  void Continue(fuchsia::modular::StoryState state, fit::function<void()> at) {
    continue_state_ = state;
    continue_ = std::move(at);
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

  // |fuchsia::modular::StoryWatcher|
  void OnModuleFocused(std::vector<std::string> module_path) override {}

  fidl::Binding<fuchsia::modular::StoryWatcher> binding_;

  fuchsia::modular::StoryState continue_state_{
      fuchsia::modular::StoryState::STOPPED};
  fit::function<void()> continue_{[] {}};

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
  void Watch(fuchsia::modular::Link* const link) {
    link->WatchAll(binding_.NewBinding());
  }

  // Deregisters itself from the watched story.
  void Reset() { binding_.Unbind(); }

  // Sets the function where to continue when the story is observed to be done.
  void Continue(fit::function<void(fidl::StringPtr)> at) {
    continue_ = std::move(at);
  }

 private:
  // |fuchsia::modular::LinkWatcher|
  void Notify(fuchsia::mem::Buffer value) override {
    std::string json;
    FXL_CHECK(fsl::StringFromVmo(value, &json));
    continue_(json);
  }

  fidl::Binding<fuchsia::modular::LinkWatcher> binding_;

  fit::function<void(fidl::StringPtr)> continue_{[](fidl::StringPtr) {}};

  FXL_DISALLOW_COPY_AND_ASSIGN(LinkWatcherImpl);
};

// Measures timing the machinery available to a session shell implementation.
// This is invoked as a session shell from basemgr and executes a predefined
// sequence of steps, rather than to expose a UI to be driven by user
// interaction, as a session shell normally would.
class TestApp : public modular::ViewApp {
 public:
  TestApp(component::StartupContext* const startup_context, Settings settings)
      : ViewApp(startup_context), settings_(std::move(settings)) {
    startup_context->ConnectToEnvironmentService(
        session_shell_context_.NewRequest());
    session_shell_context_->GetStoryProvider(story_provider_.NewRequest());

    startup_context->ConnectToEnvironmentService(puppet_master_.NewRequest());

    startup_context->outgoing().AddPublicService(
        session_shell_impl_.GetHandler());

    tracing_waiter_.WaitForTracing([this] { Loop(); });
  }

  ~TestApp() override = default;

  // Called by AppDriver in ComponentMain(). NOTE(mesch): Even though it
  // overrides SingleServiceApp::Terminate(), it is called directly on TestApp
  // by AppDriver, so it must not be private.
  void Terminate(fit::function<void()> done) override {
    // The corresponding BEGIN() call is in Loop(), below.
    TRACE_ASYNC_END("benchmark", "user/logout", 0);
    done();
  }

 private:
  void Loop() {
    if (story_count_ < settings_.story_count) {
      FXL_LOG(INFO) << "Loop at " << story_count_ << " of "
                    << settings_.story_count;
      async::PostTask(async_get_default_dispatcher(),
                      [this] { StoryCreate(); });

    } else {
      TRACE_ASYNC_BEGIN("benchmark", "user/logout", 0);
      session_shell_context_->Logout();
    }
  }

  void StoryCreate() {
    FXL_LOG(INFO) << "StoryCreate()";
    TRACE_ASYNC_BEGIN("benchmark", "story/create", 0);
    std::string story_name =
        std::string(kStoryNamePrefix) + std::to_string(story_count_);
    puppet_master_->ControlStory(story_name, story_puppet_master_.NewRequest());

    std::vector<fuchsia::modular::StoryCommand> commands;
    fuchsia::modular::AddMod add_mod;
    add_mod.mod_name_transitional = "root";
    add_mod.intent.handler = settings_.module_url;
    add_mod.intent.action = "action";

    fuchsia::modular::StoryCommand command;
    command.set_add_mod(std::move(add_mod));
    commands.push_back(std::move(command));

    story_puppet_master_->Enqueue(std::move(commands));
    story_puppet_master_->Execute(
        [this, story_name](fuchsia::modular::ExecuteResult result) {
          TRACE_ASYNC_END("benchmark", "story/create", 0);
          StoryInfo(story_name);
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
    std::vector<std::string> root_module_path;
    root_module_path.push_back(kRootModuleName);
    link_path.module_path = std::move(root_module_path);
    link_path.link_name = nullptr;
    story_controller_->GetLink(std::move(link_path), link_.NewRequest());

    link_watcher_.Watch(link_.get());
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
    story_watcher_.Continue(fuchsia::modular::StoryState::RUNNING, [] {
      TRACE_ASYNC_END("benchmark", "story/start", 0);
    });

    story_watcher_.Watch(story_controller_.get());
    story_controller_->RequestStart();
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
  modular::testing::SessionShellImpl session_shell_impl_;

  fuchsia::modular::PuppetMasterPtr puppet_master_;
  fuchsia::modular::StoryPuppetMasterPtr story_puppet_master_;

  fuchsia::modular::SessionShellContextPtr session_shell_context_;
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
