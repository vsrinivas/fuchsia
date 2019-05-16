// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/modular/cpp/fidl.h>
#include <fuchsia/modular/testing/cpp/fidl.h>
#include <lib/modular_test_harness/cpp/test_harness_fixture.h>
#include <sdk/lib/sys/cpp/component_context.h>
#include <sdk/lib/sys/cpp/service_directory.h>
#include <sdk/lib/sys/cpp/testing/test_with_environment.h>
#include <src/lib/fxl/logging.h>

#include "peridot/lib/testing/session_shell_impl.h"

namespace {

class LastFocusTimeTest : public modular::testing::TestHarnessFixture {};

constexpr char kServiceRootPath[] = "/svc";

std::unique_ptr<sys::ComponentContext> CreateComponentContext(
    fuchsia::sys::StartupInfo startup_info) {
  fuchsia::sys::FlatNamespace& flat = startup_info.flat_namespace;
  if (flat.paths.size() != flat.directories.size()) {
    return nullptr;
  }

  zx::channel service_root;
  for (size_t i = 0; i < flat.paths.size(); ++i) {
    if (flat.paths.at(i) == kServiceRootPath) {
      service_root = std::move(flat.directories.at(i));
      break;
    }
  }

  return std::make_unique<sys::ComponentContext>(
      std::make_unique<sys::ServiceDirectory>(std::move(service_root)),
      std::move(startup_info.launch_info.directory_request));
}

// A basic mock session shell component: gives access to services
// available to session shells in their environment, as well as an
// implementation of fuchsia::modular::SessionShell built for tests.
class TestSessionShell {
 public:
  TestSessionShell(std::unique_ptr<sys::ComponentContext> component_context)
      : component_context_(std::move(component_context)) {
    component_context_->svc()->Connect(session_shell_context_.NewRequest());
    session_shell_context_->GetStoryProvider(story_provider_.NewRequest());

    component_context_->outgoing()->AddPublicService(
        session_shell_impl_.GetHandler());
  }

  sys::ComponentContext* component_context() {
    return component_context_.get();
  }

  modular::testing::SessionShellImpl* session_shell_impl() {
    return &session_shell_impl_;
  }

  fuchsia::modular::SessionShellContext* session_shell_context() {
    return session_shell_context_.get();
  }

  fuchsia::modular::StoryProvider* story_provider() {
    return story_provider_.get();
  }

 private:
  std::unique_ptr<sys::ComponentContext> component_context_;
  modular::testing::SessionShellImpl session_shell_impl_;
  fuchsia::modular::SessionShellContextPtr session_shell_context_;
  fuchsia::modular::StoryProviderPtr story_provider_;
};

// A basic mock module component. It doesn't do anything but the minimal
// set of operations needed to be a module.
class TestModule {
 public:
  TestModule(std::unique_ptr<sys::ComponentContext> component_context)
      : component_context_(std::move(component_context)) {}

  sys::ComponentContext* component_context() {
    return component_context_.get();
  }

 private:
  std::unique_ptr<sys::ComponentContext> component_context_;
};

// A simple story provider watcher implementation. It confirms that it sees an
// increase in the last_focus_time in the fuchsia::modular::StoryInfo it
// receives, and pushes the test through to the next step.
class TestStoryProviderWatcher : public fuchsia::modular::StoryProviderWatcher {
 public:
  TestStoryProviderWatcher() : binding_(this) {}
  ~TestStoryProviderWatcher() override = default;

  void OnChange(fit::function<void(fuchsia::modular::StoryInfo)> on_change) {
    on_change_ = std::move(on_change);
  }

  void Watch(fuchsia::modular::StoryProvider* const story_provider) {
    story_provider->Watch(binding_.NewBinding());
  }

 private:
  // |fuchsia::modular::StoryProviderWatcher|
  void OnDelete(::std::string story_id) override {}

  // |fuchsia::modular::StoryProviderWatcher|
  void OnChange(
      fuchsia::modular::StoryInfo story_info,
      fuchsia::modular::StoryState story_state,
      fuchsia::modular::StoryVisibilityState story_visibility_state) override {
    on_change_(std::move(story_info));
    return;
  }

  fit::function<void(fuchsia::modular::StoryInfo)> on_change_;
  fidl::Binding<fuchsia::modular::StoryProviderWatcher> binding_;
};

class TestStoryWatcher : fuchsia::modular::StoryWatcher {
 public:
  TestStoryWatcher() : binding_(this) {}
  ~TestStoryWatcher() override = default;

  // Registers itself as a watcher on the given story. Only one story at a time
  // can be watched.
  void Watch(fuchsia::modular::StoryController* const story_controller) {
    story_controller->Watch(binding_.NewBinding());
  }

  // Sets the function where to continue when the story is observed to be
  // running.
  void OnStoryRunning(fit::function<void()> at) { continue_ = std::move(at); }

 private:
  // |fuchsia::modular::StoryWatcher|
  void OnStateChange(fuchsia::modular::StoryState state) override {
    FXL_LOG(INFO) << "OnStateChange() " << fidl::ToUnderlying(state);
    if (state != fuchsia::modular::StoryState::RUNNING) {
      return;
    }

    continue_();
  }

  // |fuchsia::modular::StoryWatcher|
  void OnModuleAdded(fuchsia::modular::ModuleData /*module_data*/) override {}

  // |fuchsia::modular::StoryWatcher|
  void OnModuleFocused(std::vector<std::string> /*module_path*/) override {}

  fit::function<void()> continue_;
  fidl::Binding<fuchsia::modular::StoryWatcher> binding_;
};
}  // namespace

const char kStoryName[] = "storyname";

TEST_F(LastFocusTimeTest, LastFocusTimeIncreases) {
  fuchsia::modular::testing::TestHarnessSpec spec;
  const auto test_session_shell_url = InterceptSessionShell(&spec,
                                                            R"(
      {
        "sandbox": {
          "services": [
            "fuchsia.modular.SessionShellContext",
            "fuchsia.modular.PuppetMaster"
          ]
        }
      })");

  // And listen for the module we're going to create.
  const auto test_module_url = GenerateFakeUrl();
  fuchsia::modular::testing::InterceptSpec intercept_spec;
  intercept_spec.set_component_url(test_module_url);
  spec.mutable_components_to_intercept()->push_back(std::move(intercept_spec));

  // Listen for interception of components we're implementing here and
  // assign them to local variables.
  std::unique_ptr<TestSessionShell> test_session_shell;
  std::unique_ptr<TestModule> test_module;
  test_harness().events().OnNewComponent =
      [&](fuchsia::sys::StartupInfo startup_info,
          fidl::InterfaceHandle<fuchsia::modular::testing::InterceptedComponent>
              component) {
        if (startup_info.launch_info.url == test_session_shell_url) {
          test_session_shell = std::make_unique<TestSessionShell>(
              CreateComponentContext(std::move(startup_info)));
        } else if (startup_info.launch_info.url == test_module_url) {
          test_module = std::make_unique<TestModule>(
              CreateComponentContext(std::move(startup_info)));
        } else {
          FXL_LOG(FATAL) << "Unexpected component URL: "
                         << startup_info.launch_info.url;
        }
      };

  test_harness()->Run(std::move(spec));

  // Wait for our session shell to start.
  RunLoopUntil([&] { return !!test_session_shell; });

  // Connect to extra services also provided to session shells.
  fuchsia::modular::PuppetMasterPtr puppet_master;
  test_session_shell->component_context()->svc()->Connect(
      puppet_master.NewRequest());

  fuchsia::modular::FocusControllerPtr focus_controller;
  fuchsia::modular::FocusProviderPtr focus_provider;
  test_session_shell->session_shell_context()->GetFocusController(
      focus_controller.NewRequest());
  test_session_shell->session_shell_context()->GetFocusProvider(
      focus_provider.NewRequest());

  // Watch for changes to the session.
  TestStoryProviderWatcher story_provider_watcher;
  story_provider_watcher.Watch(test_session_shell->story_provider());

  // Keep track of the focus timestamps that we receive for the story created
  // below so we can assert that they make sense at the end of the test.
  std::vector<int64_t> last_focus_timestamps;
  story_provider_watcher.OnChange([&](fuchsia::modular::StoryInfo story_info) {
    ASSERT_EQ(kStoryName, story_info.id);
    last_focus_timestamps.push_back(story_info.last_focus_time);
  });

  // Create a story so that we can signal the framework to focus it.
  fuchsia::modular::StoryPuppetMasterPtr story_puppet_master;
  puppet_master->ControlStory(kStoryName, story_puppet_master.NewRequest());

  fuchsia::modular::AddMod add_mod;
  add_mod.mod_name_transitional = "modname";
  add_mod.intent.handler = test_module_url;
  add_mod.intent.action = "action";

  std::vector<fuchsia::modular::StoryCommand> commands(1);
  commands.at(0).set_add_mod(std::move(add_mod));

  story_puppet_master->Enqueue(std::move(commands));
  bool story_created{false};
  story_puppet_master->Execute(
      [&](fuchsia::modular::ExecuteResult result) { story_created = true; });
  RunLoopUntil([&] { return story_created; });

  // Watch the story and then start it.
  TestStoryWatcher story_watcher;
  fuchsia::modular::StoryControllerPtr story_controller;
  test_session_shell->story_provider()->GetController(
      kStoryName, story_controller.NewRequest());
  story_watcher.Watch(story_controller.get());
  story_controller->RequestStart();

  story_watcher.OnStoryRunning([&] {
    // Focus the story!
    focus_controller->Set(kStoryName);
  });

  // Run until we have been notified of new last_focus_time values three times.
  // We expect a call for each of:
  // 1) The story is created.
  // 2) The story transitions to running.
  // 3) The story is focused.
  RunLoopUntil([&]() { return last_focus_timestamps.size() == 3; });
  EXPECT_EQ(0, last_focus_timestamps[0]);
  EXPECT_EQ(0, last_focus_timestamps[1]);
  EXPECT_LT(0, last_focus_timestamps[2]);
}
