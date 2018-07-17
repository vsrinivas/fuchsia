// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Implementation of a user shell for module development. It takes a
// root module URL and data for its fuchsia::modular::Link as command line
// arguments, which can be set using the device_runner --user-shell-args flag.

#include <memory>
#include <utility>

#include <fuchsia/modular/cpp/fidl.h>
#include <fuchsia/sys/cpp/fidl.h>
#include <fuchsia/ui/views_v1/cpp/fidl.h>
#include <fuchsia/ui/views_v1_token/cpp/fidl.h>
#include <lib/app_driver/cpp/app_driver.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/component/cpp/connect.h>
#include <lib/component/cpp/startup_context.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/fxl/command_line.h>
#include <lib/fxl/logging.h>
#include <lib/fxl/macros.h>

#include "peridot/lib/common/names.h"
#include "peridot/lib/fidl/single_service_app.h"
#include "peridot/lib/fidl/view_host.h"
#include "peridot/lib/rapidjson/rapidjson.h"
#include "peridot/lib/testing/test_driver.h"

namespace {

class Settings {
 public:
  explicit Settings(const fxl::CommandLine& command_line) {
    root_module =
        command_line.GetOptionValueWithDefault("root_module", "example_recipe");
    root_link = command_line.GetOptionValueWithDefault("root_link", "");
    story_id = command_line.GetOptionValueWithDefault("story_id", "");
    module_under_test_url =
        command_line.GetOptionValueWithDefault("module_under_test_url", "");
    test_driver_url =
        command_line.GetOptionValueWithDefault("test_driver_url", "");
  }

  std::string root_module;
  std::string root_link;
  std::string story_id;
  std::string module_under_test_url;
  std::string test_driver_url;
};

class DevUserShellApp
    : fuchsia::modular::StoryWatcher,
      fuchsia::modular::InterruptionListener,
      fuchsia::modular::NextListener,
      public modular::SingleServiceApp<fuchsia::modular::UserShell> {
 public:
  explicit DevUserShellApp(component::StartupContext* const startup_context,
                           Settings settings)
      : SingleServiceApp(startup_context),
        settings_(std::move(settings)),
        story_watcher_binding_(this) {}

  ~DevUserShellApp() override = default;

 private:
  // |SingleServiceApp|
  void CreateView(
      fidl::InterfaceRequest<fuchsia::ui::views_v1_token::ViewOwner>
          view_owner_request,
      fidl::InterfaceRequest<fuchsia::sys::ServiceProvider> /*services*/)
      override {
    view_owner_request_ = std::move(view_owner_request);
    Connect();
  }

  // |fuchsia::modular::UserShell|
  void Initialize(fidl::InterfaceHandle<fuchsia::modular::UserShellContext>
                      user_shell_context) override {
    user_shell_context_.Bind(std::move(user_shell_context));
    user_shell_context_->GetStoryProvider(story_provider_.NewRequest());
    user_shell_context_->GetSuggestionProvider(
        suggestion_provider_.NewRequest());
    user_shell_context_->GetFocusController(focus_controller_.NewRequest());
    user_shell_context_->GetVisibleStoriesController(
        visible_stories_controller_.NewRequest());

    suggestion_provider_->SubscribeToInterruptions(
        interruption_listener_bindings_.AddBinding(this));
    suggestion_provider_->SubscribeToNext(
        next_listener_bindings_.AddBinding(this), 3);

    Connect();
  }

  void Connect() {
    if (!view_owner_request_ || !story_provider_) {
      // Not yet ready, wait for the other of CreateView() and
      // Initialize() to be called.
      return;
    }

    FXL_LOG(INFO) << "DevUserShell START " << settings_.root_module << " "
                  << settings_.root_link;

    view_ = std::make_unique<modular::ViewHost>(
        startup_context()
            ->ConnectToEnvironmentService<fuchsia::ui::views_v1::ViewManager>(),
        std::move(view_owner_request_));

    if (settings_.story_id.empty()) {
      story_provider_->CreateStory(nullptr,
                                   [this](const fidl::StringPtr& story_id) {
                                     StartStoryById(story_id);
                                   });
    } else {
      StartStoryById(settings_.story_id);
    }
  }

  fidl::VectorPtr<fuchsia::modular::IntentParameter> CreateIntentParameters() {
    if (settings_.module_under_test_url.empty() ||
        settings_.test_driver_url.empty()) {
      // For debugging: log that both items must be set in the event that one is
      // set and the other is not. It may be unclear why the intent is not being
      // created with the intended links if one is forgotten by accident.
      if (settings_.module_under_test_url.empty() !=
          settings_.test_driver_url.empty()) {
        FXL_LOG(WARNING) << "Both the module_under_test_url and "
                            "test_driver_url must be set";
      }
      return nullptr;
    }
    auto intent_params =
        fidl::VectorPtr<fuchsia::modular::IntentParameter>::New(0);
    fuchsia::modular::IntentParameterData test_driver_link_data;

    rapidjson::Document document;
    document.SetObject();
    document.AddMember(modular::testing::kModuleUnderTestPath,
                       settings_.module_under_test_url,
                       document.GetAllocator());
    document.AddMember(modular::testing::kTestDriverPath,
                       settings_.test_driver_url, document.GetAllocator());
    test_driver_link_data.set_json(modular::JsonValueToString(document));

    fuchsia::modular::IntentParameter test_driver_link_param;
    test_driver_link_param.name = modular::testing::kTestDriverLinkName;
    test_driver_link_param.data = std::move(test_driver_link_data);
    intent_params.push_back(std::move(test_driver_link_param));

    return intent_params;
  }

  void StartStoryById(const fidl::StringPtr& story_id) {
    story_provider_->GetController(story_id, story_controller_.NewRequest());
    story_controller_.set_error_handler([this, story_id] {
      FXL_LOG(ERROR) << "Story controller for story " << story_id
                     << " died. Does this story exist?";
    });

    fuchsia::modular::Intent intent;
    intent.handler = settings_.root_module;
    intent.parameters = CreateIntentParameters();
    story_controller_->AddModule(nullptr, modular::kRootModuleName,
                                 std::move(intent), nullptr);

    story_controller_->Watch(story_watcher_binding_.NewBinding());

    FXL_LOG(INFO) << "DevUserShell Starting story with id: " << story_id;
    fidl::InterfaceHandle<fuchsia::ui::views_v1_token::ViewOwner>
        root_module_view;
    story_controller_->Start(root_module_view.NewRequest());
    view_->ConnectView(std::move(root_module_view));
    focus_controller_->Set(story_id);
    auto visible_stories = fidl::VectorPtr<fidl::StringPtr>::New(0);
    visible_stories.push_back(story_id);
    visible_stories_controller_->Set(std::move(visible_stories));

    if (!settings_.root_link.empty()) {
      fuchsia::modular::LinkPtr root;

      fuchsia::modular::LinkPath link_path = fuchsia::modular::LinkPath();
      link_path.module_path = ::fidl::VectorPtr<::fidl::StringPtr>::New(0);
      link_path.link_name = "root";
      story_controller_->GetLink(std::move(link_path), root.NewRequest());

      root->Set(nullptr, settings_.root_link);
    }
  }

  // |fuchsia::modular::StoryWatcher|
  void OnStateChange(fuchsia::modular::StoryState state) override {
    FXL_LOG(INFO) << "DevUserShell State " << state;
  }

  // |fuchsia::modular::StoryWatcher|
  void OnModuleAdded(fuchsia::modular::ModuleData /*module_data*/) override {}

  // |fuchsia::modular::NextListener|
  void OnNextResults(
      fidl::VectorPtr<fuchsia::modular::Suggestion> suggestions) override {
    FXL_VLOG(4)
        << "DevUserShell/fuchsia::modular::NextListener::OnNextResults()";
    for (auto& suggestion : *suggestions) {
      FXL_LOG(INFO) << "  " << suggestion.uuid << " "
                    << suggestion.display.headline;
    }
  }

  // |fuchsia::modular::InterruptionListener|
  void OnInterrupt(fuchsia::modular::Suggestion suggestion) override {
    FXL_VLOG(4)
        << "DevUserShell/fuchsia::modular::InterruptionListener::OnInterrupt() "
        << suggestion.uuid;
  }

  // |fuchsia::modular::NextListener|
  void OnProcessingChange(bool processing) override {
    FXL_VLOG(4)
        << "DevUserShell/fuchsia::modular::NextListener::OnProcessingChange("
        << processing << ")";
  }

  const Settings settings_;

  fidl::InterfaceRequest<fuchsia::ui::views_v1_token::ViewOwner>
      view_owner_request_;
  std::unique_ptr<modular::ViewHost> view_;

  fuchsia::modular::UserShellContextPtr user_shell_context_;
  fuchsia::modular::StoryProviderPtr story_provider_;
  fuchsia::modular::StoryControllerPtr story_controller_;
  fuchsia::modular::FocusControllerPtr focus_controller_;
  fuchsia::modular::VisibleStoriesControllerPtr visible_stories_controller_;

  fidl::Binding<fuchsia::modular::StoryWatcher> story_watcher_binding_;

  fuchsia::modular::SuggestionProviderPtr suggestion_provider_;
  fidl::BindingSet<fuchsia::modular::InterruptionListener>
      interruption_listener_bindings_;
  fidl::BindingSet<fuchsia::modular::NextListener> next_listener_bindings_;

  FXL_DISALLOW_COPY_AND_ASSIGN(DevUserShellApp);
};

}  // namespace

int main(int argc, const char** argv) {
  auto command_line = fxl::CommandLineFromArgcArgv(argc, argv);
  Settings settings(command_line);

  async::Loop loop(&kAsyncLoopConfigAttachToThread);

  auto context = component::StartupContext::CreateFromStartupInfo();
  modular::AppDriver<DevUserShellApp> driver(
      context->outgoing().deprecated_services(),
      std::make_unique<DevUserShellApp>(context.get(), std::move(settings)),
      [&loop] { loop.Quit(); });

  loop.Run();
  return 0;
}
