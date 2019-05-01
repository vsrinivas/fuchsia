// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Implementation of a session shell for module development. It takes a
// root module URL and data for its fuchsia::modular::Link as command line
// arguments, which can be set using the basemgr --user-shell-args flag.

#include <fuchsia/modular/cpp/fidl.h>
#include <fuchsia/sys/cpp/fidl.h>
#include <fuchsia/ui/scenic/cpp/fidl.h>
#include <fuchsia/ui/views/cpp/fidl.h>
#include <fuchsia/ui/viewsv1token/cpp/fidl.h>
#include <lib/app_driver/cpp/app_driver.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/component/cpp/connect.h>
#include <lib/component/cpp/startup_context.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/fsl/vmo/strings.h>
#include <lib/ui/scenic/cpp/view_token_pair.h>
#include <lib/zx/eventpair.h>
#include <src/lib/fxl/command_line.h>
#include <src/lib/fxl/logging.h>
#include <src/lib/fxl/macros.h>

#include <memory>
#include <utility>

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
    story_id = command_line.GetOptionValueWithDefault("story_id", "story");
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

class DevSessionShellApp : fuchsia::modular::StoryWatcher,
                           fuchsia::modular::InterruptionListener,
                           fuchsia::modular::NextListener,
                           fuchsia::modular::SessionShell,
                           public modular::ViewApp {
 public:
  explicit DevSessionShellApp(component::StartupContext* const startup_context,
                              Settings settings)
      : ViewApp(startup_context),
        settings_(std::move(settings)),
        story_watcher_binding_(this) {
    startup_context->ConnectToEnvironmentService(puppet_master_.NewRequest());
    startup_context->ConnectToEnvironmentService(
        session_shell_context_.NewRequest());
    session_shell_context_->GetStoryProvider(story_provider_.NewRequest());
    session_shell_context_->GetSuggestionProvider(
        suggestion_provider_.NewRequest());
    session_shell_context_->GetFocusController(focus_controller_.NewRequest());
    session_shell_context_->GetVisibleStoriesController(
        visible_stories_controller_.NewRequest());

    suggestion_provider_->SubscribeToInterruptions(
        interruption_listener_bindings_.AddBinding(this));
    suggestion_provider_->SubscribeToNext(
        next_listener_bindings_.AddBinding(this), 3);

    startup_context->outgoing().AddPublicService(
        session_shell_bindings_.GetHandler(this));
  }

  ~DevSessionShellApp() override = default;

 private:
  // |ViewApp|
  void CreateView(
      zx::eventpair view_token,
      fidl::InterfaceRequest<
          fuchsia::sys::ServiceProvider> /*incoming_services*/,
      fidl::InterfaceHandle<
          fuchsia::sys::ServiceProvider> /*outgoing_services*/) override {
    view_token_.value = std::move(view_token);

    Connect();
  }

  void Connect() {
    FXL_CHECK(!!view_token_.value);
    FXL_CHECK(!!story_provider_);
    FXL_CHECK(!!puppet_master_);
    FXL_LOG(INFO) << "DevSessionShell START " << settings_.root_module << " "
                  << settings_.root_link;

    auto scenic =
        startup_context()
            ->ConnectToEnvironmentService<fuchsia::ui::scenic::Scenic>();
    scenic::ViewContext context = {
        .session_and_listener_request =
            scenic::CreateScenicSessionPtrAndListenerRequest(scenic.get()),
        .view_token2 = std::move(view_token_),
        .startup_context = startup_context(),
    };

    view_ = std::make_unique<modular::ViewHost>(std::move(context));

    puppet_master_->ControlStory(settings_.story_id,
                                 story_puppet_master_.NewRequest());

    std::vector<fuchsia::modular::StoryCommand> commands;
    fuchsia::modular::AddMod add_mod;
    add_mod.mod_name_transitional = "root";
    add_mod.intent.handler = settings_.root_module;
    add_mod.intent.action = "action";
    add_mod.intent.parameters = CreateIntentParameters();

    fuchsia::modular::StoryCommand command;
    command.set_add_mod(std::move(add_mod));
    commands.push_back(std::move(command));

    story_puppet_master_->Enqueue(std::move(commands));
    story_puppet_master_->Execute(
        [this](fuchsia::modular::ExecuteResult result) {
          StartStoryById(settings_.story_id);
        });
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
    fsl::SizedVmo vmo;
    FXL_CHECK(fsl::VmoFromString(modular::JsonValueToString(document), &vmo));
    test_driver_link_data.set_json(std::move(vmo).ToTransport());

    fuchsia::modular::IntentParameter test_driver_link_param;
    test_driver_link_param.name = modular::testing::kTestDriverLinkName;
    test_driver_link_param.data = std::move(test_driver_link_data);
    intent_params.push_back(std::move(test_driver_link_param));

    return intent_params;
  }

  void StartStoryById(const fidl::StringPtr& story_id) {
    story_provider_->GetController(story_id, story_controller_.NewRequest());
    story_controller_.set_error_handler([this, story_id](zx_status_t status) {
      FXL_LOG(ERROR) << "Story controller for story " << story_id
                     << " died. Does this story exist?";
    });

    story_controller_->Watch(story_watcher_binding_.NewBinding());

    FXL_LOG(INFO) << "DevSessionShell Starting story with id: " << story_id;

    story_controller_->RequestStart();
    focus_controller_->Set(story_id);
    auto visible_stories = fidl::VectorPtr<std::string>::New(0);
    visible_stories.push_back(story_id);
    visible_stories_controller_->Set(std::move(visible_stories));

    if (!settings_.root_link.empty()) {
      fuchsia::modular::LinkPtr root;

      fuchsia::modular::LinkPath link_path = fuchsia::modular::LinkPath();
      link_path.link_name = "root";
      story_controller_->GetLink(std::move(link_path), root.NewRequest());

      fsl::SizedVmo vmo;
      FXL_CHECK(fsl::VmoFromString(settings_.root_link, &vmo));
      root->Set(nullptr, std::move(vmo).ToTransport());
    }
  }

  // |SessionShell|
  void AttachView(fuchsia::modular::ViewIdentifier view_id,
                  fidl::InterfaceHandle<fuchsia::ui::viewsv1token::ViewOwner>
                      view_owner) override {
    AttachView2(view_id, scenic::ToViewHolderToken(zx::eventpair(
                             view_owner.TakeChannel().release())));
  }

  // |SessionShell|
  void AttachView2(
      fuchsia::modular::ViewIdentifier view_id,
      fuchsia::ui::views::ViewHolderToken view_holder_token) override {
    FXL_LOG(INFO) << "DevSessionShell AttachView(): " << view_id.story_id;
    view_->ConnectView(std::move(view_holder_token));
  }

  // |SessionShell|
  void DetachView(fuchsia::modular::ViewIdentifier view_id,
                  fit::function<void()> done) override {
    FXL_LOG(INFO) << "DevSessionShell DetachView(): " << view_id.story_id;
    done();
  }

  // |fuchsia::modular::StoryWatcher|
  void OnStateChange(fuchsia::modular::StoryState state) override {
    FXL_LOG(INFO) << "DevSessionShell State " << fidl::ToUnderlying(state);
  }

  // |fuchsia::modular::StoryWatcher|
  void OnModuleAdded(fuchsia::modular::ModuleData /*module_data*/) override {}

  // |fuchsia::modular::StoryWatcher|
  void OnModuleFocused(std::vector<std::string> /*module_path*/) override {}

  // |fuchsia::modular::NextListener|
  void OnNextResults(
      std::vector<fuchsia::modular::Suggestion> suggestions) override {
    FXL_VLOG(4)
        << "DevSessionShell/fuchsia::modular::NextListener::OnNextResults()";
    for (auto& suggestion : suggestions) {
      FXL_LOG(INFO) << "  " << suggestion.uuid << " "
                    << suggestion.display.headline;
    }
  }

  // |fuchsia::modular::InterruptionListener|
  void OnInterrupt(fuchsia::modular::Suggestion suggestion) override {
    FXL_VLOG(4) << "DevSessionShell/"
                   "fuchsia::modular::InterruptionListener::OnInterrupt() "
                << suggestion.uuid;
  }

  // |fuchsia::modular::NextListener|
  void OnProcessingChange(bool processing) override {
    FXL_VLOG(4)
        << "DevSessionShell/fuchsia::modular::NextListener::OnProcessingChange("
        << processing << ")";
  }

  const Settings settings_;

  fidl::BindingSet<fuchsia::modular::SessionShell> session_shell_bindings_;

  fuchsia::ui::views::ViewToken view_token_;
  std::unique_ptr<modular::ViewHost> view_;

  fuchsia::modular::SessionShellContextPtr session_shell_context_;
  fuchsia::modular::PuppetMasterPtr puppet_master_;
  fuchsia::modular::StoryPuppetMasterPtr story_puppet_master_;
  fuchsia::modular::StoryProviderPtr story_provider_;
  fuchsia::modular::StoryControllerPtr story_controller_;
  fuchsia::modular::FocusControllerPtr focus_controller_;
  fuchsia::modular::VisibleStoriesControllerPtr visible_stories_controller_;

  fidl::Binding<fuchsia::modular::StoryWatcher> story_watcher_binding_;

  fuchsia::modular::SuggestionProviderPtr suggestion_provider_;
  fidl::BindingSet<fuchsia::modular::InterruptionListener>
      interruption_listener_bindings_;
  fidl::BindingSet<fuchsia::modular::NextListener> next_listener_bindings_;

  FXL_DISALLOW_COPY_AND_ASSIGN(DevSessionShellApp);
};

}  // namespace

int main(int argc, const char** argv) {
  auto command_line = fxl::CommandLineFromArgcArgv(argc, argv);
  Settings settings(command_line);

  async::Loop loop(&kAsyncLoopConfigAttachToThread);

  auto context = component::StartupContext::CreateFromStartupInfo();
  modular::AppDriver<DevSessionShellApp> driver(
      context->outgoing().deprecated_services(),
      std::make_unique<DevSessionShellApp>(context.get(), std::move(settings)),
      [&loop] { loop.Quit(); });

  loop.Run();
  return 0;
}
