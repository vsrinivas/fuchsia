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
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/sys/cpp/component_context.h>

#include <memory>
#include <utility>

#include "src/lib/fxl/command_line.h"
#include "src/lib/fxl/macros.h"
#include "src/lib/syslog/cpp/logger.h"
#include "src/modular/lib/app_driver/cpp/app_driver.h"
#include "src/modular/lib/fidl/single_service_app.h"
#include "src/modular/lib/fidl/view_host.h"

namespace {

class Settings {
 public:
  explicit Settings(const fxl::CommandLine& command_line) {
    root_module = command_line.GetOptionValueWithDefault("root_module", "example_recipe");
    story_id = command_line.GetOptionValueWithDefault("story_id", "story");
  }

  std::string root_module;
  std::string story_id;
};

class DevSessionShellApp : fuchsia::modular::StoryWatcher,
                           fuchsia::modular::SessionShell,
                           public modular::ViewApp {
 public:
  explicit DevSessionShellApp(sys::ComponentContext* const component_context, Settings settings)
      : ViewApp(component_context), settings_(std::move(settings)), story_watcher_binding_(this) {
    component_context->svc()->Connect(puppet_master_.NewRequest());
    component_context->svc()->Connect(session_shell_context_.NewRequest());
    session_shell_context_->GetStoryProvider(story_provider_.NewRequest());
    session_shell_context_->GetFocusController(focus_controller_.NewRequest());

    component_context->outgoing()->AddPublicService(session_shell_bindings_.GetHandler(this));

    component_context_ = sys::ComponentContext::Create();
  }

  ~DevSessionShellApp() override = default;

 private:
  // |ViewApp|
  void CreateView(
      zx::eventpair view_token,
      fidl::InterfaceRequest<fuchsia::sys::ServiceProvider> /*incoming_services*/,
      fidl::InterfaceHandle<fuchsia::sys::ServiceProvider> /*outgoing_services*/) override {
    view_token_.value = std::move(view_token);

    Connect();
  }

  void Connect() {
    FX_CHECK(!!view_token_.value);
    FX_CHECK(!!story_provider_);
    FX_CHECK(!!puppet_master_);
    FX_LOGS(INFO) << "DevSessionShell START " << settings_.root_module;

    auto scenic = component_context()->svc()->Connect<fuchsia::ui::scenic::Scenic>();
    scenic::ViewContext context = {
        .session_and_listener_request =
            scenic::CreateScenicSessionPtrAndListenerRequest(scenic.get()),
        .view_token = std::move(view_token_),
        .component_context = component_context_.get(),
    };

    view_ = std::make_unique<modular::ViewHost>(std::move(context));

    puppet_master_->ControlStory(settings_.story_id, story_puppet_master_.NewRequest());

    std::vector<fuchsia::modular::StoryCommand> commands;
    fuchsia::modular::AddMod add_mod;
    add_mod.mod_name_transitional = "root";
    add_mod.intent.handler = settings_.root_module;
    add_mod.intent.action = "action";

    fuchsia::modular::StoryCommand command;
    command.set_add_mod(std::move(add_mod));
    commands.push_back(std::move(command));

    story_puppet_master_->Enqueue(std::move(commands));
    story_puppet_master_->Execute(
        [this](fuchsia::modular::ExecuteResult result) { StartStoryById(settings_.story_id); });
  }

  void StartStoryById(const fidl::StringPtr& story_id) {
    story_provider_->GetController(story_id.value_or(""), story_controller_.NewRequest());
    story_controller_.set_error_handler([story_id](zx_status_t status) {
      FX_LOGS(ERROR) << "Story controller for story " << story_id
                     << " died. Does this story exist?";
    });

    story_controller_->Watch(story_watcher_binding_.NewBinding());

    FX_LOGS(INFO) << "DevSessionShell Starting story with id: " << story_id;

    story_controller_->RequestStart();
    focus_controller_->Set(story_id);
  }

  // |SessionShell|
  void AttachView(fuchsia::modular::ViewIdentifier view_id,
                  fuchsia::ui::views::ViewHolderToken view_holder_token) override {
    FX_LOGS(INFO) << "DevSessionShell AttachView(): " << view_id.story_id;
    view_->ConnectView(std::move(view_holder_token));
  }

  // |SessionShell|
  void AttachView2(fuchsia::modular::ViewIdentifier view_id,
                   fuchsia::ui::views::ViewHolderToken view_holder_token) override {
    AttachView(view_id, std::move(view_holder_token));
  }

  // |SessionShell|
  void DetachView(fuchsia::modular::ViewIdentifier view_id, fit::function<void()> done) override {
    FX_LOGS(INFO) << "DevSessionShell DetachView(): " << view_id.story_id;
    done();
  }

  // |fuchsia::modular::StoryWatcher|
  void OnStateChange(fuchsia::modular::StoryState state) override {
    FX_LOGS(INFO) << "DevSessionShell State " << fidl::ToUnderlying(state);
  }

  // |fuchsia::modular::StoryWatcher|
  void OnModuleAdded(fuchsia::modular::ModuleData /*module_data*/) override {}

  // |fuchsia::modular::StoryWatcher|
  void OnModuleFocused(std::vector<std::string> /*module_path*/) override {}

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

  fidl::Binding<fuchsia::modular::StoryWatcher> story_watcher_binding_;

  std::unique_ptr<sys::ComponentContext> component_context_;

  FXL_DISALLOW_COPY_AND_ASSIGN(DevSessionShellApp);
};

}  // namespace

int main(int argc, const char** argv) {
  syslog::InitLogger({"dev_session_shell"});
  
  auto command_line = fxl::CommandLineFromArgcArgv(argc, argv);
  Settings settings(command_line);

  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);

  auto context = sys::ComponentContext::Create();
  modular::AppDriver<DevSessionShellApp> driver(
      context->outgoing(), std::make_unique<DevSessionShellApp>(context.get(), std::move(settings)),
      [&loop] { loop.Quit(); });

  loop.Run();
  return 0;
}
