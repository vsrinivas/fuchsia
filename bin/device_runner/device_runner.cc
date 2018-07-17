// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cmath>
#include <iostream>
#include <memory>
#include <string>

#include <fs/pseudo-file.h>
#include <fuchsia/modular/auth/cpp/fidl.h>
#include <fuchsia/modular/cpp/fidl.h>
#include <fuchsia/sys/cpp/fidl.h>
#include <fuchsia/ui/policy/cpp/fidl.h>
#include <fuchsia/ui/viewsv1/cpp/fidl.h>
#include <fuchsia/ui/viewsv1token/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async/cpp/future.h>
#include <lib/component/cpp/startup_context.h>
#include <lib/fidl/cpp/array.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/fidl/cpp/interface_handle.h>
#include <lib/fidl/cpp/interface_request.h>
#include <lib/fidl/cpp/string.h>
#include <lib/fxl/command_line.h>
#include <lib/fxl/logging.h>
#include <lib/fxl/macros.h>
#include <trace-provider/provider.h>

#include "peridot/bin/device_runner/cobalt/cobalt.h"
#include "peridot/bin/device_runner/user_provider_impl.h"
#include "peridot/lib/common/async_holder.h"
#include "peridot/lib/common/names.h"
#include "peridot/lib/common/teardown.h"
#include "peridot/lib/fidl/app_client.h"
#include "peridot/lib/fidl/array_to_string.h"
#include "peridot/lib/fidl/clone.h"
#include "peridot/lib/user_shell_settings/user_shell_settings.h"
#include "peridot/lib/util/filesystem.h"

namespace modular {

namespace {

class Settings {
 public:
  explicit Settings(const fxl::CommandLine& command_line) {
    device_shell.url = command_line.GetOptionValueWithDefault(
        "device_shell", "userpicker_device_shell");
    story_shell.url =
        command_line.GetOptionValueWithDefault("story_shell", "mondrian");
    user_runner.url =
        command_line.GetOptionValueWithDefault("user_runner", "user_runner");
    user_shell.url = command_line.GetOptionValueWithDefault(
        "user_shell", "ermine_user_shell");
    account_provider.url = command_line.GetOptionValueWithDefault(
        "account_provider", "oauth_token_manager");

    disable_statistics = command_line.HasOption("disable_statistics");
    ignore_monitor = command_line.HasOption("ignore_monitor");
    no_minfs = command_line.HasOption("no_minfs");
    test = command_line.HasOption("test");
    enable_presenter = command_line.HasOption("enable_presenter");

    ParseShellArgs(
        command_line.GetOptionValueWithDefault("device_shell_args", ""),
        &device_shell.args);

    ParseShellArgs(
        command_line.GetOptionValueWithDefault("story_shell_args", ""),
        &story_shell.args);

    ParseShellArgs(
        command_line.GetOptionValueWithDefault("user_runner_args", ""),
        &user_runner.args);

    ParseShellArgs(
        command_line.GetOptionValueWithDefault("user_shell_args", ""),
        &user_shell.args);

    if (test) {
      device_shell.args.push_back("--test");
      story_shell.args.push_back("--test");
      user_runner.args.push_back("--test");
      user_shell.args.push_back("--test");
      test_name = FindTestName(user_shell.url, user_shell.args);
      disable_statistics = true;
      ignore_monitor = true;
      no_minfs = true;
    }
  }

  static std::string GetUsage() {
    return R"USAGE(device_runner
      --device_shell=DEVICE_SHELL
      --device_shell_args=SHELL_ARGS
      --user_shell=USER_SHELL
      --user_shell_args=SHELL_ARGS
      --story_shell=STORY_SHELL
      --story_shell_args=SHELL_ARGS
      --account_provider=ACCOUNT_PROVIDER
      --disable_statistics
      --ignore_monitor
      --no_minfs
      --test
      --enable_presenter
    DEVICE_NAME: Name which user shell uses to identify this device.
    DEVICE_SHELL: URL of the device shell to run.
                Defaults to "userpicker_device_shell".
                For integration testing use "dev_device_shell".
    USER_RUNNER: URL of the user runner to run.
                Defaults to "user_runner".
    USER_SHELL: URL of the user shell to run.
                Defaults to "ermine_user_shell".
                For integration testing use "dev_user_shell".
    STORY_SHELL: URL of the story shell to run.
                Defaults to "mondrian".
                For integration testing use "dev_story_shell".
    SHELL_ARGS: Comma separated list of arguments. Backslash escapes comma.
    ACCOUNT_PROVIDER: URL of the account provider to use.
                Defaults to "oauth_token_manager".
                For integration tests use "dev_token_manager".)USAGE";
  }

  fuchsia::modular::AppConfig device_shell;
  fuchsia::modular::AppConfig story_shell;
  fuchsia::modular::AppConfig user_runner;
  fuchsia::modular::AppConfig user_shell;
  fuchsia::modular::AppConfig account_provider;

  std::string test_name;
  bool disable_statistics;
  bool ignore_monitor;
  bool no_minfs;
  bool test;
  bool enable_presenter;

 private:
  void ParseShellArgs(const std::string& value,
                      fidl::VectorPtr<fidl::StringPtr>* args) {
    bool escape = false;
    std::string arg;
    for (char i : value) {
      if (escape) {
        arg.push_back(i);
        escape = false;
        continue;
      }

      if (i == '\\') {
        escape = true;
        continue;
      }

      if (i == ',') {
        args->push_back(arg);
        arg.clear();
        continue;
      }

      arg.push_back(i);
    }

    if (!arg.empty()) {
      args->push_back(arg);
    }
  }

  // Extract the test name using knowledge of how Modular structures its
  // command lines for testing.
  static std::string FindTestName(
      const fidl::StringPtr& user_shell,
      const fidl::VectorPtr<fidl::StringPtr>& user_shell_args) {
    const std::string kRootModule = "--root_module";
    std::string result = user_shell;

    for (const auto& user_shell_arg : *user_shell_args) {
      const auto& arg = user_shell_arg.get();
      if (arg.substr(0, kRootModule.size()) == kRootModule) {
        result = arg.substr(kRootModule.size());
      }
    }

    const auto index = result.find_last_of('/');
    if (index == std::string::npos) {
      return result;
    }

    return result.substr(index + 1);
  }

  FXL_DISALLOW_COPY_AND_ASSIGN(Settings);
};

}  // namespace

class DeviceRunnerApp : fuchsia::modular::DeviceShellContext,
                        fuchsia::modular::auth::AccountProviderContext,
                        fuchsia::ui::policy::KeyboardCaptureListenerHACK,
                        modular::UserProviderImpl::Delegate {
 public:
  explicit DeviceRunnerApp(
      const Settings& settings,
      std::shared_ptr<component::StartupContext> const context,
      std::function<void()> on_shutdown)
      : settings_(settings),
        user_provider_impl_("UserProviderImpl"),
        context_(std::move(context)),
        on_shutdown_(std::move(on_shutdown)),
        device_shell_context_binding_(this),
        account_provider_context_binding_(this) {
    if (!context_->has_environment_services()) {
      FXL_LOG(ERROR) << "Failed to receive services from the environment.";
      exit(1);
    }

    // TODO(SCN-595): Presentation is now discoverable, so we don't need
    // kPresentationService anymore.
    service_namespace_.AddService(presentation_state_.bindings.GetHandler(
                                      presentation_state_.presentation.get()),
                                  kPresentationService);

    if (settings.ignore_monitor) {
      Start();
      return;
    }

    context_->ConnectToEnvironmentService(monitor_.NewRequest());

    monitor_.set_error_handler([] {
      FXL_LOG(ERROR) << "No device runner monitor found.";
      exit(1);
    });

    monitor_->GetConnectionCount([this](uint32_t count) {
      if (count != 1) {
        FXL_LOG(ERROR) << "Another device runner is running."
                       << " Please use that one, or shut it down first.";
        exit(1);
      }

      Start();
    });
  }

 private:
  void StartDeviceShell() {
    if (device_shell_running_) {
      FXL_DLOG(INFO) << "StartDeviceShell() called when already running";

      return;
    }

    device_shell_app_ =
        std::make_unique<AppClient<fuchsia::modular::Lifecycle>>(
            context_->launcher().get(), CloneStruct(settings_.device_shell));
    device_shell_app_->services().ConnectToService(device_shell_.NewRequest());

    fuchsia::ui::viewsv1::ViewProviderPtr device_shell_view_provider;
    device_shell_app_->services().ConnectToService(
        device_shell_view_provider.NewRequest());

    // We still need to pass a request for root view to device shell since
    // dev_device_shell (which mimics flutter behavior) blocks until it receives
    // the root view request.
    fidl::InterfaceHandle<fuchsia::ui::viewsv1token::ViewOwner> root_view;
    fuchsia::ui::policy::PresentationPtr presentation;
    device_shell_view_provider->CreateView(root_view.NewRequest(), nullptr);
    // |enable_presenter| overrides |test| for running the presenter service.
    if (!settings_.test || settings_.enable_presenter) {
      context_->ConnectToEnvironmentService<fuchsia::ui::policy::Presenter>()
          ->Present(std::move(root_view), presentation.NewRequest());
      AddGlobalKeyboardShortcuts(presentation);
    }

    // Populate parameters and initialize the device shell.
    fuchsia::modular::DeviceShellParams params;
    params.presentation = std::move(presentation);
    device_shell_->Initialize(device_shell_context_binding_.NewBinding(),
                              std::move(params));

    device_shell_running_ = true;
  }

  FuturePtr<> StopDeviceShell() {
    if (!device_shell_running_) {
      FXL_DLOG(INFO) << "StopDeviceShell() called when already stopped";

      return Future<>::CreateCompleted("StopDeviceShell::Completed");
    }

    auto did_stop = Future<>::Create("StopDeviceShell");

    device_shell_app_->Teardown(kBasicTimeout, [did_stop, this] {
      FXL_DLOG(INFO) << "- fuchsia::modular::DeviceShell down";

      device_shell_running_ = false;
      did_stop->Complete();
    });

    return did_stop;
  }

  void Start() {
    if (settings_.test) {
      // 0. Print test banner.
      FXL_LOG(INFO)
          << std::endl
          << std::endl
          << "======================== Starting Test [" << settings_.test_name
          << "]" << std::endl
          << "============================================================"
          << std::endl;
    }

    // Start the device shell. This is done first so that we can show some UI
    // until other things come up.
    StartDeviceShell();

    // Wait for persistent data to come up.
    if (!settings_.no_minfs) {
      WaitForMinfs();
    }

    // Start OAuth Token Manager App.
    fuchsia::modular::AppConfig token_manager_config;
    token_manager_config.url = settings_.account_provider.url;
    token_manager_ =
        std::make_unique<AppClient<fuchsia::modular::auth::AccountProvider>>(
            context_->launcher().get(), std::move(token_manager_config),
            "/data/modular/ACCOUNT_MANAGER");
    token_manager_->SetAppErrorHandler([] {
      FXL_CHECK(false) << "Token manager crashed. Stopping device runner.";
    });
    token_manager_->primary_service()->Initialize(
        account_provider_context_binding_.NewBinding());

    user_provider_impl_.reset(new UserProviderImpl(
        context_, settings_.user_runner, settings_.user_shell,
        settings_.story_shell, token_manager_->primary_service().get(), this));

    ReportEvent(ModularEvent::BOOTED_TO_DEVICE_RUNNER);
  }

  // |fuchsia::modular::DeviceShellContext|
  void GetUserProvider(
      fidl::InterfaceRequest<fuchsia::modular::UserProvider> request) override {
    user_provider_impl_->Connect(std::move(request));
  }

  // |fuchsia::modular::DeviceShellContext|
  void Shutdown() override {
    // TODO(mesch): Some of these could be done in parallel too.
    // fuchsia::modular::UserProvider must go first, but the order after user
    // provider is for now rather arbitrary. We terminate device shell last so
    // that in tests testing::Teardown() is invoked at the latest possible time.
    // Right now it just demonstrates that AppTerminate() works as we like it
    // to.
    FXL_DLOG(INFO) << "fuchsia::modular::DeviceShellContext::Shutdown()";

    if (settings_.test) {
      FXL_LOG(INFO)
          << std::endl
          << "============================================================"
          << std::endl
          << "======================== [" << settings_.test_name << "] Done";
    }

    user_provider_impl_.Teardown(kUserProviderTimeout, [this] {
      FXL_DLOG(INFO) << "- fuchsia::modular::UserProvider down";
      token_manager_->Teardown(kBasicTimeout, [this] {
        FXL_DLOG(INFO) << "- AuthProvider down";
        StopDeviceShell()->Then([this] {
          FXL_LOG(INFO) << "Clean Shutdown";
          on_shutdown_();
        });
      });
    });
  }

  // |AccountProviderContext|
  void GetAuthenticationContext(
      fidl::StringPtr account_id,
      fidl::InterfaceRequest<fuchsia::modular::auth::AuthenticationContext>
          request) override {
    // TODO(MI4-1107): DeviceRunner needs to implement AuthenticationContext
    // itself, and proxy calls for StartOverlay & StopOverlay to DeviceShell,
    // starting it if it's not running yet.

    device_shell_->GetAuthenticationContext(account_id, std::move(request));
  }

  // |UserProviderImpl::Delegate|
  void DidLogin() override {
    if (settings_.test) {
      // TODO(MI4-1117): Integration tests currently expect device shell to
      // always be running. So, if we're running under a test, do not shut down
      // the device shell after login.
      return;
    }

    FXL_DLOG(INFO) << "Stopping device shell due to login";

    StopDeviceShell();

    auto presentation_request =
        presentation_state_.presentation.is_bound()
            ? presentation_state_.presentation.Unbind().NewRequest()
            : presentation_state_.presentation.NewRequest();

    context_->ConnectToEnvironmentService<fuchsia::ui::policy::Presenter>()
        ->Present(std::move(user_shell_view_owner_),
                  std::move(presentation_request));

    AddGlobalKeyboardShortcuts(presentation_state_.presentation);

    const auto& settings_vector = UserShellSettings::GetSystemSettings();
    if (active_user_shell_index_ >= settings_vector.size()) {
      FXL_LOG(ERROR) << "Active user shell index is "
                     << active_user_shell_index_ << ", but only "
                     << settings_vector.size() << " user shells exist.";
      return;
    }

    UpdatePresentation(settings_vector[active_user_shell_index_]);
  }

  // |UserProviderImpl::Delegate|
  void DidLogout() override {
    if (settings_.test) {
      // TODO(MI4-1117): Integration tests currently expect device shell to
      // always be running. So, if we're running under a test, DidLogin() will
      // not shut down the device shell after login; thus this method doesn't
      // need to re-start the device shell after a logout.
      return;
    }

    FXL_DLOG(INFO) << "Re-starting device shell due to logout";

    StartDeviceShell();
  }

  // |UserProviderImpl::Delegate|
  fidl::InterfaceRequest<fuchsia::ui::viewsv1token::ViewOwner>
  GetUserShellViewOwner(
      fidl::InterfaceRequest<fuchsia::ui::viewsv1token::ViewOwner>) override {
    return user_shell_view_owner_.is_bound()
               ? user_shell_view_owner_.Unbind().NewRequest()
               : user_shell_view_owner_.NewRequest();
  }

  // |UserProviderImpl::Delegate|
  fidl::InterfaceHandle<fuchsia::sys::ServiceProvider>
  GetUserShellServiceProvider(
      fidl::InterfaceHandle<fuchsia::sys::ServiceProvider>) override {
    fidl::InterfaceHandle<fuchsia::sys::ServiceProvider> handle;
    service_namespace_.AddBinding(handle.NewRequest());
    return handle;
  }

  // |KeyboardCaptureListenerHACK|
  void OnEvent(fuchsia::ui::input::KeyboardEvent event) override {
    switch (event.code_point) {
      case ' ': {
        SwapUserShell();
        break;
      }
      case 's': {
        SetNextShadowTechnique();
        break;
      }
      case 'l':
        ToggleClipping();
        break;
      default:
        FXL_DLOG(INFO) << "Unknown keyboard event: codepoint="
                       << event.code_point << ", modifiers=" << event.modifiers;
        break;
    }
  }

  void AddGlobalKeyboardShortcuts(
      fuchsia::ui::policy::PresentationPtr& presentation) {
    presentation->CaptureKeyboardEventHACK(
        {
            .code_point = ' ',  // spacebar
            .modifiers = fuchsia::ui::input::kModifierLeftControl,
        },
        keyboard_capture_listener_bindings_.AddBinding(this));
    presentation->CaptureKeyboardEventHACK(
        {
            .code_point = 's',
            .modifiers = fuchsia::ui::input::kModifierLeftControl,
        },
        keyboard_capture_listener_bindings_.AddBinding(this));
    presentation->CaptureKeyboardEventHACK(
        {
            .code_point = 'l',
            .modifiers = fuchsia::ui::input::kModifierRightAlt,
        },
        keyboard_capture_listener_bindings_.AddBinding(this));
  }

  void UpdatePresentation(const UserShellSettings& settings) {
    if (settings.display_usage != fuchsia::ui::policy::DisplayUsage::kUnknown) {
      FXL_DLOG(INFO) << "Setting display usage: "
                     << fidl::ToUnderlying(settings.display_usage);
      presentation_state_.presentation->SetDisplayUsage(settings.display_usage);
    }

    if (!std::isnan(settings.screen_width) &&
        !std::isnan(settings.screen_height)) {
      FXL_DLOG(INFO) << "Setting display size: " << settings.screen_width
                     << " x " << settings.screen_height;
      presentation_state_.presentation->SetDisplaySizeInMm(
          settings.screen_width, settings.screen_height);
    }
  }

  void SwapUserShell() {
    if (UserShellSettings::GetSystemSettings().empty()) {
      FXL_DLOG(INFO) << "No user shells has been defined";
      return;
    }

    active_user_shell_index_ = (active_user_shell_index_ + 1) %
                               UserShellSettings::GetSystemSettings().size();
    const auto& settings =
        UserShellSettings::GetSystemSettings().at(active_user_shell_index_);

    auto user_shell_config = fuchsia::modular::AppConfig::New();
    user_shell_config->url = settings.name;

    user_provider_impl_->SwapUserShell(std::move(*user_shell_config))->Then([] {
      FXL_DLOG(INFO) << "Swapped user shell";
    });
  }

  void SetNextShadowTechnique() {
    using ShadowTechnique = fuchsia::ui::gfx::ShadowTechnique;

    auto next_shadow_technique =
        [](ShadowTechnique shadow_technique) -> ShadowTechnique {
      switch (shadow_technique) {
        case ShadowTechnique::UNSHADOWED:
          return ShadowTechnique::SCREEN_SPACE;
        case ShadowTechnique::SCREEN_SPACE:
          return ShadowTechnique::SHADOW_MAP;
        default:
          FXL_LOG(ERROR) << "Unknown shadow technique: "
                         << fidl::ToUnderlying(shadow_technique);
          // Fallthrough
        case ShadowTechnique::SHADOW_MAP:
        case ShadowTechnique::MOMENT_SHADOW_MAP:
          return ShadowTechnique::UNSHADOWED;
      }
    };

    presentation_state_.shadow_technique =
        next_shadow_technique(presentation_state_.shadow_technique);

    fuchsia::ui::gfx::RendererParam param;
    param.set_shadow_technique(presentation_state_.shadow_technique);

    FXL_DLOG(INFO) << "Setting shadow technique to "
                   << fidl::ToUnderlying(presentation_state_.shadow_technique);
    auto renderer_params =
        fidl::VectorPtr<fuchsia::ui::gfx::RendererParam>::New(0);
    renderer_params.push_back(std::move(param));
    presentation_state_.presentation->SetRendererParams(
        std::move(renderer_params));
  }

  void ToggleClipping() {
    FXL_DLOG(INFO) << "Toggling clipping";

    presentation_state_.clipping_enabled =
        !presentation_state_.clipping_enabled;
    presentation_state_.presentation->EnableClipping(
        presentation_state_.clipping_enabled);
  }

  const Settings& settings_;  // Not owned nor copied.

  AsyncHolder<UserProviderImpl> user_provider_impl_;

  std::shared_ptr<component::StartupContext> const context_;
  fuchsia::modular::DeviceRunnerMonitorPtr monitor_;
  std::function<void()> on_shutdown_;

  fidl::Binding<fuchsia::modular::DeviceShellContext>
      device_shell_context_binding_;
  fidl::Binding<fuchsia::modular::auth::AccountProviderContext>
      account_provider_context_binding_;

  std::unique_ptr<AppClient<fuchsia::modular::auth::AccountProvider>>
      token_manager_;

  bool device_shell_running_{};
  std::unique_ptr<AppClient<fuchsia::modular::Lifecycle>> device_shell_app_;
  fuchsia::modular::DeviceShellPtr device_shell_;

  fidl::BindingSet<fuchsia::ui::policy::KeyboardCaptureListenerHACK>
      keyboard_capture_listener_bindings_;

  fuchsia::ui::viewsv1token::ViewOwnerPtr user_shell_view_owner_;

  struct {
    fuchsia::ui::policy::PresentationPtr presentation;
    fidl::BindingSet<fuchsia::ui::policy::Presentation> bindings;

    fuchsia::ui::gfx::ShadowTechnique shadow_technique{};
    bool clipping_enabled{};
  } presentation_state_;

  component::ServiceNamespace service_namespace_;

  std::vector<UserShellSettings>::size_type active_user_shell_index_{};

  FXL_DISALLOW_COPY_AND_ASSIGN(DeviceRunnerApp);
};

fxl::AutoCall<fit::closure> SetupCobalt(Settings& settings,
                                        async_dispatcher_t* dispatcher,
                                        component::StartupContext* context) {
  if (settings.disable_statistics) {
    return fxl::MakeAutoCall<fit::closure>([] {});
  }
  return InitializeCobalt(dispatcher, context);
};

}  // namespace modular

int main(int argc, const char** argv) {
  auto command_line = fxl::CommandLineFromArgcArgv(argc, argv);
  if (command_line.HasOption("help")) {
    std::cout << modular::Settings::GetUsage() << std::endl;
    return 0;
  }

  modular::Settings settings(command_line);
  async::Loop loop(&kAsyncLoopConfigAttachToThread);
  trace::TraceProvider trace_provider(loop.dispatcher());
  auto context = std::shared_ptr<component::StartupContext>(
      component::StartupContext::CreateFromStartupInfo());
  fxl::AutoCall<fit::closure> cobalt_cleanup = modular::SetupCobalt(
      settings, std::move(loop.dispatcher()), context.get());

  modular::DeviceRunnerApp app(settings, context, [&loop, &cobalt_cleanup] {
    cobalt_cleanup.call();
    loop.Quit();
  });
  loop.Run();

  return 0;
}
