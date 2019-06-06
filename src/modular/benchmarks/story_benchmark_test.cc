// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/modular/cpp/fidl.h>
#include <fuchsia/sys/cpp/fidl.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/fsl/vmo/strings.h>
#include <lib/modular_test_harness/cpp/fake_component.h>
#include <lib/modular_test_harness/cpp/fake_module.h>
#include <lib/modular_test_harness/cpp/test_harness_fixture.h>
#include <src/lib/fxl/logging.h>
#include <src/lib/fxl/macros.h>
#include <src/lib/fxl/strings/string_number_conversions.h>
#include <trace/event.h>

#include <memory>
#include <utility>

#include "peridot/lib/testing/session_shell_impl.h"
#include "src/modular/benchmarks/tracing_waiter.h"

namespace {

// Number of stories to create in the Loop test.
const int kStoryCount = 5;

// Number of times each module sets its link value.
const int kLinkSetCount = 100;

class TestStoryWatcher : fuchsia::modular::StoryWatcher {
 public:
  TestStoryWatcher() : binding_(this) {}
  ~TestStoryWatcher() override = default;

  // Registers itself as a watcher on the given story. Only one story at a time
  // can be watched.
  void Watch(fuchsia::modular::StoryController* const story_controller) {
    story_controller->Watch(binding_.NewBinding());
  }

  // Deregisters itself from the watched story.
  void Reset() { binding_.Unbind(); }

  // Sets a callback that will be called once the story is running.
  void OnStoryRunning(fit::function<void()> on_running) {
    on_running_ = std::move(on_running);
  }

 private:
  // |fuchsia::modular::StoryWatcher|
  void OnStateChange(fuchsia::modular::StoryState state) override {
    FXL_LOG(INFO) << "TestStoryWatcher.OnStateChange(): "
                  << fidl::ToUnderlying(state);
    if (state != fuchsia::modular::StoryState::RUNNING) {
      return;
    }

    on_running_();
  }

  // |fuchsia::modular::StoryWatcher|
  void OnModuleAdded(fuchsia::modular::ModuleData /*module_data*/) override {}

  // |fuchsia::modular::StoryWatcher|
  void OnModuleFocused(std::vector<std::string> /*module_path*/) override {}

  fit::function<void()> on_running_;
  fidl::Binding<fuchsia::modular::StoryWatcher> binding_;
};

// A simple link watcher implementation that invokes a callback when
// it sees the watched link change.
class TestLinkWatcher : fuchsia::modular::LinkWatcher {
 public:
  TestLinkWatcher() : binding_(this) {}
  ~TestLinkWatcher() override = default;

  // Registers itself as a watcher on the given link. Only one story at a time
  // can be watched.
  void Watch(fuchsia::modular::Link* const link) {
    link->WatchAll(binding_.NewBinding());
  }

  // Deregisters itself from the watched story.
  void Reset() { binding_.Unbind(); }

  // Sets the function that is called when the link changes.
  void OnNotify(fit::function<void(fidl::StringPtr)> callback) {
    on_notify_ = std::move(callback);
  }

 private:
  // |fuchsia::modular::LinkWatcher|
  void Notify(fuchsia::mem::Buffer value) override {
    std::string json;
    FXL_CHECK(fsl::StringFromVmo(value, &json));
    on_notify_(std::move(json));
  }

  fidl::Binding<fuchsia::modular::LinkWatcher> binding_;

  fit::function<void(fidl::StringPtr)> on_notify_{[](fidl::StringPtr) {}};

  FXL_DISALLOW_COPY_AND_ASSIGN(TestLinkWatcher);
};

// A basic fake session shell component: gives access to services
// available to session shells in their environment, as well as an
// implementation of fuchsia::modular::SessionShell built for tests.
class TestSessionShell : public modular::testing::FakeComponent {
 public:
  fuchsia::modular::StoryProvider* story_provider() {
    return story_provider_.get();
  }

  fuchsia::modular::SessionShellContext* session_shell_context() {
    return session_shell_context_.get();
  }

 private:
  // |modular::testing::FakeComponent|
  void OnCreate(fuchsia::sys::StartupInfo startup_info) override {
    component_context()->svc()->Connect(session_shell_context_.NewRequest());
    session_shell_context_->GetStoryProvider(story_provider_.NewRequest());

    component_context()->outgoing()->AddPublicService(
        session_shell_impl_.GetHandler());
  }

  modular::testing::SessionShellImpl session_shell_impl_;
  fuchsia::modular::SessionShellContextPtr session_shell_context_;
  fuchsia::modular::StoryProviderPtr story_provider_;
};

// This module repeatedly updates its root link a number of times and then
// just sits there until it's terminated.
class TestModule : public modular::testing::FakeModule,
                   fuchsia::modular::LinkWatcher {
 public:
  TestModule() : link_watcher_binding_(this) {}

 private:
  // |modular::testing::FakeComponent|
  void OnCreate(fuchsia::sys::StartupInfo startup_info) override {
    modular::testing::FakeModule::OnCreate(std::move(startup_info));

    FXL_LOG(INFO) << "TestModule.OnCreate()";

    module_context()->GetLink(nullptr, link_.NewRequest());

    // Will call Notify() with current value.
    link_->WatchAll(link_watcher_binding_.NewBinding());
  }

  // |fuchsia::modular::LinkWatcher|
  void Notify(fuchsia::mem::Buffer content) override {
    std::string json;
    FXL_CHECK(fsl::StringFromVmo(content, &json));
    FXL_LOG(INFO) << "TestModule.Notify(): " << json;

    // First invocation is from WatchAll(); next from Set().
    if (count_ == -1) {
      count_ = 0;
      Set();
      return;
    }

    // Corresponding TRACE_ASYNC_BEGIN() is in Set().
    TRACE_ASYNC_END("benchmark", "link/set", count_);

    ++count_;
    if (count_ <= kLinkSetCount) {
      Set();
    }
  }

  void Set() {
    FXL_LOG(INFO) << "TestModule.Set(): " << count_;

    // Corresponding TRACE_ASYNC_END() is in Notify().
    TRACE_ASYNC_BEGIN("benchmark", "link/set", count_);

    // Corresponding TRACE_FLOW_END() is in the session shell.
    TRACE_FLOW_BEGIN("benchmark", "link/trans", count_);

    fsl::SizedVmo vmo;
    FXL_CHECK(fsl::VmoFromString(std::to_string(count_), &vmo));
    link_->Set(nullptr, std::move(vmo).ToTransport());
  }

  // The number of times the root link has been set.
  int count_{-1};

  fuchsia::modular::LinkPtr link_;
  fidl::Binding<fuchsia::modular::LinkWatcher> link_watcher_binding_;
};

// Measures timing the machinery available to a session shell implementation.
class StoryBenchmarkTest : public modular::testing::TestHarnessFixture {
 public:
  // Name of the module created in CreateStory().
  const std::string kModName = "mod";

  // Prefix of the name of each story created.
  const std::string kStoryNamePrefix = "story-";

  // Initializes and starts the modular test harness.
  void InitSession() {
    modular::testing::TestHarnessBuilder builder;

    link_watcher_ = std::make_unique<TestLinkWatcher>();
    story_watcher_ = std::make_unique<TestStoryWatcher>();

    session_shell_ = std::make_unique<TestSessionShell>();
    builder.InterceptSessionShell(
        session_shell_->GetOnCreateHandler(),
        {.sandbox_services = {"fuchsia.modular.SessionShellContext",
                              "fuchsia.modular.PuppetMaster"}});

    // Listen for the module that is created in CreateStory().
    module_ = std::make_unique<TestModule>();
    module_url_ = builder.GenerateFakeUrl();
    builder.InterceptComponent(
        module_->GetOnCreateHandler(),
        {.url = module_url_,
         .sandbox_services = module_->GetSandboxServices()});

    test_harness().events().OnNewComponent =
        builder.BuildOnNewComponentHandler();

    TRACE_ASYNC_BEGIN("benchmark", "session/start", 0);
    test_harness()->Run(builder.BuildSpec());

    // Wait for our session shell to start.
    RunLoopUntil([&] {
      bool is_running = session_shell_->is_running();
      if (is_running) {
        TRACE_ASYNC_END("benchmark", "session/start", 0);
      }
      return is_running;
    });

    // Connect to the PuppetMaster service also provided to the session shell.
    fuchsia::modular::testing::ModularService modular_service;
    modular_service.set_puppet_master(puppet_master_.NewRequest());
    test_harness()->ConnectToModularService(std::move(modular_service));
  }

  void CreateStory(std::string story_name) {
    FXL_LOG(INFO) << "CreateStory()";
    TRACE_ASYNC_BEGIN("benchmark", "story/create", 0);

    story_name_ = story_name;

    puppet_master_->ControlStory(story_name_,
                                 story_puppet_master_.NewRequest());

    fuchsia::modular::AddMod add_mod;
    add_mod.mod_name_transitional = kModName;
    add_mod.intent.handler = module_url_;
    add_mod.intent.action = "action";

    std::vector<fuchsia::modular::StoryCommand> commands(1);
    commands.at(0).set_add_mod(std::move(add_mod));

    story_puppet_master_->Enqueue(std::move(commands));

    bool is_created{false};
    story_puppet_master_->Execute([&](fuchsia::modular::ExecuteResult result) {
      TRACE_ASYNC_END("benchmark", "story/create", 0);
      is_created = true;
    });

    // Wait for the story to be created.
    RunLoopUntil([&] { return is_created; });

    session_shell_->story_provider()->GetController(
        story_name_, story_controller_.NewRequest());
  }

  void StoryInfo() {
    FXL_LOG(INFO) << "StoryInfo()";
    TRACE_ASYNC_BEGIN("benchmark", "story/info", 0);

    bool got_story_info{false};
    story_controller_->GetInfo([&](fuchsia::modular::StoryInfo story_info,
                                   fuchsia::modular::StoryState state) {
      TRACE_ASYNC_END("benchmark", "story/info", 0);
      got_story_info = true;
    });

    // Wait for the story info to be returned.
    RunLoopUntil([&] { return got_story_info; });
  }

  void StartStory() {
    FXL_LOG(INFO) << "StartStory()";
    TRACE_ASYNC_BEGIN("benchmark", "story/start", 0);

    bool is_started{false};
    story_watcher_->OnStoryRunning([&] {
      TRACE_ASYNC_END("benchmark", "story/start", 0);
      is_started = true;
    });

    story_watcher_->Watch(story_controller_.get());
    story_controller_->RequestStart();

    // Wait for the story to start.
    RunLoopUntil([&] { return is_started; });
  }

  void WatchLink() {
    FXL_LOG(INFO) << "WatchLink()";

    std::vector<std::string> module_path = {kModName};
    fuchsia::modular::LinkPath link_path{.module_path = std::move(module_path),
                                         .link_name = nullptr};
    story_controller_->GetLink(std::move(link_path), link_.NewRequest());

    link_watcher_->Watch(link_.get());

    link_watcher_->OnNotify([&](fidl::StringPtr json) {
      FXL_LOG(INFO) << "WatchLink(): " << json;
      // Ignore empty links which have the JSON string value "null".
      if (json == "null") {
        return;
      }

      link_value_ = fxl::StringToNumber<int>(json.get());

      // Corresponding TRACE_FLOW_BEGIN() is in the module.
      TRACE_FLOW_END("benchmark", "link/trans", link_value_);
    });
  }

  void StopStory() {
    FXL_LOG(INFO) << "StopStory()";
    TRACE_ASYNC_BEGIN("benchmark", "story/stop", 0);

    bool is_stopped{false};
    story_controller_->Stop([&] {
      TRACE_ASYNC_END("benchmark", "story/stop", 0);
      is_stopped = true;
    });

    // Wait for the story to stop.
    RunLoopUntil([&] { return is_stopped; });
  }

  void Reset() {
    FXL_LOG(INFO) << "Reset()";
    link_watcher_->Reset();
    story_watcher_->Reset();
    story_controller_.Unbind();
    story_puppet_master_.Unbind();
    story_name_.clear();
  }

  void Logout() {
    FXL_LOG(INFO) << "Logout()";
    TRACE_ASYNC_BEGIN("benchmark", "user/logout", 0);
    session_shell_->session_shell_context()->Logout();
    TRACE_ASYNC_END("benchmark", "user/logout", 0);
  }

  // The name of the story created by CreateStory().
  std::string story_name_;

  // Component URL of the |module_| intercepted in InitSession().
  std::string module_url_;

  // The last link value that |link_watcher_| has observed.
  int link_value_{0};

  std::unique_ptr<TestStoryWatcher> story_watcher_;
  std::unique_ptr<TestSessionShell> session_shell_;
  std::unique_ptr<TestModule> module_;
  std::unique_ptr<TestLinkWatcher> link_watcher_;

  fuchsia::modular::StoryControllerPtr story_controller_;
  fuchsia::modular::PuppetMasterPtr puppet_master_;
  fuchsia::modular::StoryPuppetMasterPtr story_puppet_master_;
  fuchsia::modular::LinkPtr link_;

  modular::TracingWaiter tracing_waiter_;
};

TEST_F(StoryBenchmarkTest, Loop) {
  // Wait for the tracing service to be ready to use.
  bool is_tracing_started{false};
  tracing_waiter_.WaitForTracing([&] { is_tracing_started = true; });
  RunLoopUntil([&] { return is_tracing_started; });

  InitSession();

  for (int i = 1; i <= kStoryCount; i++) {
    auto story_name = std::string(kStoryNamePrefix) + std::to_string(i);

    FXL_LOG(INFO) << "Creating story \"" << story_name << "\" (" << i << " of "
                  << kStoryCount << ")";

    CreateStory(story_name);
    StoryInfo();
    WatchLink();
    StartStory();

    // Wait for the module to set the link value |kLinkSetCount| times.
    RunLoopUntil([&] { return link_value_ == kLinkSetCount; });

    StopStory();

    Reset();
  }

  Logout();
}

}  // namespace
