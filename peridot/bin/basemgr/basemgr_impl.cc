// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/basemgr/basemgr_impl.h"

#include <memory>

#include <fuchsia/auth/cpp/fidl.h>
#include <fuchsia/modular/auth/cpp/fidl.h>
#include <fuchsia/modular/cpp/fidl.h>
#include <fuchsia/sys/cpp/fidl.h>
#include <fuchsia/ui/policy/cpp/fidl.h>
#include <fuchsia/ui/viewsv1/cpp/fidl.h>
#include <fuchsia/ui/viewsv1token/cpp/fidl.h>
#include <lib/async/cpp/future.h>
#include <lib/component/cpp/startup_context.h>
#include <lib/fidl/cpp/interface_handle.h>
#include <lib/fidl/cpp/interface_request.h>
#include <lib/fidl/cpp/string.h>
#include <lib/fxl/logging.h>
#include <lib/fxl/macros.h>

#include "peridot/bin/basemgr/basemgr_settings.h"
#include "peridot/bin/basemgr/session_shell_settings/session_shell_settings.h"
#include "peridot/bin/basemgr/user_provider_impl.h"
#include "peridot/bin/basemgr/wait_for_minfs.h"
#include "peridot/lib/common/async_holder.h"
#include "peridot/lib/common/teardown.h"
#include "peridot/lib/fidl/app_client.h"
#include "peridot/lib/fidl/clone.h"

namespace modular {

namespace {
#ifdef AUTO_LOGIN_TO_GUEST
constexpr bool kAutoLoginToGuest = true;
#else
constexpr bool kAutoLoginToGuest = false;
#endif

// The service name of the Presentation service that is routed between
// BaseShell and SessionShell. The same service exchange between SessionShell
// and StoryShell uses the SessionShellPresentationProvider service, which is
// discoverable.
// NOTE: This is defined in user_context_impl.cc as well.
// TODO(SCN-595): mozart.Presentation is being renamed to ui.Presenter.
constexpr char kPresentationService[] = "mozart.Presentation";

// TODO(MF-134): This key is duplicated in
// topaz/lib/settings/lib/device_info.dart. Remove this key once factory reset
// is provided to topaz as a service.
// The key for factory reset toggles.
constexpr char kFactoryResetKey[] = "FactoryReset";

}  // namespace

BasemgrImpl::BasemgrImpl(
    const modular::BasemgrSettings& settings,
    const std::vector<SessionShellSettings>& session_shell_settings,
    fuchsia::sys::Launcher* const launcher,
    fuchsia::ui::policy::PresenterPtr presenter,
    fuchsia::devicesettings::DeviceSettingsManagerPtr device_settings_manager,
    std::function<void()> on_shutdown)
    : settings_(settings),
      session_shell_settings_(session_shell_settings),
      launcher_(launcher),
      presenter_(std::move(presenter)),
      device_settings_manager_(std::move(device_settings_manager)),
      on_shutdown_(std::move(on_shutdown)),
      user_provider_impl_("UserProviderImpl"),
      base_shell_context_binding_(this),
      authentication_context_provider_binding_(this) {
  UpdateSessionShellConfig();

  // TODO(SCN-595): Presentation is now discoverable, so we don't need
  // kPresentationService anymore.
  service_namespace_.AddService(presentation_state_.bindings.GetHandler(
                                    presentation_state_.presentation.get()),
                                kPresentationService);

  Start();
}

BasemgrImpl::~BasemgrImpl() = default;

void BasemgrImpl::Connect(
    fidl::InterfaceRequest<fuchsia::modular::internal::BasemgrDebug> request) {
  basemgr_bindings_.AddBinding(this, std::move(request));
}

void BasemgrImpl::InitializePresentation(
    fidl::InterfaceHandle<fuchsia::ui::viewsv1token::ViewOwner> view_owner) {
  if (settings_.test && !settings_.enable_presenter) {
    return;
  }

  auto presentation_request =
      presentation_state_.presentation.is_bound()
          ? presentation_state_.presentation.Unbind().NewRequest()
          : presentation_state_.presentation.NewRequest();

  presenter_->Present2(zx::eventpair(view_owner.TakeChannel().release()),
                       std::move(presentation_request));

  AddGlobalKeyboardShortcuts(presentation_state_.presentation);

  SetShadowTechnique(presentation_state_.shadow_technique);

  // Set the presentation of the given view to the settings of the active
  // session shell.
  if (active_session_shell_settings_index_ >= session_shell_settings_.size()) {
    FXL_LOG(ERROR) << "Active session shell index is "
                   << active_session_shell_settings_index_ << ", but only "
                   << session_shell_settings_.size()
                   << " session shell settings exist.";
    return;
  }

  auto active_session_shell_settings =
      session_shell_settings_[active_session_shell_settings_index_];
  if (active_session_shell_settings.display_usage !=
      fuchsia::ui::policy::DisplayUsage::kUnknown) {
    FXL_DLOG(INFO) << "Setting display usage: "
                   << fidl::ToUnderlying(
                          active_session_shell_settings.display_usage);
    presentation_state_.presentation->SetDisplayUsage(
        active_session_shell_settings.display_usage);
  }

  if (!std::isnan(active_session_shell_settings.screen_width) &&
      !std::isnan(active_session_shell_settings.screen_height)) {
    FXL_DLOG(INFO) << "Setting display size: "
                   << active_session_shell_settings.screen_width << " x "
                   << active_session_shell_settings.screen_height;
    presentation_state_.presentation->SetDisplaySizeInMm(
        active_session_shell_settings.screen_width,
        active_session_shell_settings.screen_height);
  }
}

void BasemgrImpl::StartBaseShell() {
  if (base_shell_running_) {
    FXL_DLOG(INFO) << "StartBaseShell() called when already running";

    return;
  }

  base_shell_app_ = std::make_unique<AppClient<fuchsia::modular::Lifecycle>>(
      launcher_, CloneStruct(settings_.base_shell));
  base_shell_app_->services().ConnectToService(base_shell_.NewRequest());

  fuchsia::ui::viewsv1::ViewProviderPtr base_shell_view_provider;
  base_shell_app_->services().ConnectToService(
      base_shell_view_provider.NewRequest());

  // We still need to pass a request for root view to base shell since
  // dev_base_shell (which mimics flutter behavior) blocks until it receives
  // the root view request.
  fidl::InterfaceHandle<fuchsia::ui::viewsv1token::ViewOwner> root_view;
  base_shell_view_provider->CreateView(root_view.NewRequest(), nullptr);

  InitializePresentation(std::move(root_view));

  // Populate parameters and initialize the base shell.
  fuchsia::modular::BaseShellParams params;
  params.presentation = std::move(presentation_state_.presentation);
  base_shell_->Initialize(base_shell_context_binding_.NewBinding(),
                          std::move(params));

  base_shell_running_ = true;
}

FuturePtr<> BasemgrImpl::StopBaseShell() {
  if (!base_shell_running_) {
    FXL_DLOG(INFO) << "StopBaseShell() called when already stopped";

    return Future<>::CreateCompleted("StopBaseShell::Completed");
  }

  auto did_stop = Future<>::Create("StopBaseShell");

  base_shell_app_->Teardown(kBasicTimeout, [did_stop, this] {
    FXL_DLOG(INFO) << "- fuchsia::modular::BaseShell down";

    base_shell_running_ = false;
    did_stop->Complete();
  });

  return did_stop;
}

FuturePtr<> BasemgrImpl::StopTokenManagerFactoryApp() {
  if (!token_manager_factory_app_) {
    FXL_DLOG(INFO)
        << "StopTokenManagerFactoryApp() called when already stopped";

    return Future<>::CreateCompleted("StopTokenManagerFactoryApp::Completed");
  }

  auto did_stop = Future<>::Create("StopTokenManagerFactoryApp");

  token_manager_factory_app_->Teardown(kBasicTimeout, [did_stop, this] {
    FXL_DLOG(INFO) << "- fuchsia::auth::TokenManagerFactory down";

    token_manager_factory_app_.release();
    did_stop->Complete();
  });

  return did_stop;
}

void BasemgrImpl::Start() {
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

  // Wait for persistent data to come up.
  if (!settings_.no_minfs) {
    WaitForMinfs();
  }

  // Start OAuth Token Manager App.
  token_manager_factory_app_.release();
  fuchsia::modular::AppConfig token_manager_config;
  token_manager_config.url = settings_.account_provider.url;
  token_manager_factory_app_ =
      std::make_unique<AppClient<fuchsia::modular::Lifecycle>>(
          launcher_, CloneStruct(token_manager_config));
  token_manager_factory_app_->services().ConnectToService(
      token_manager_factory_.NewRequest());

  user_provider_impl_.reset(new UserProviderImpl(
      launcher_, settings_.sessionmgr, session_shell_config_,
      settings_.story_shell, token_manager_factory_.get(),
      authentication_context_provider_binding_.NewBinding().Bind(), this));

  ShowSetupOrLogin();

  ReportEvent(ModularEvent::BOOTED_TO_BASEMGR);
}

void BasemgrImpl::GetUserProvider(
    fidl::InterfaceRequest<fuchsia::modular::UserProvider> request) {
  user_provider_impl_->Connect(std::move(request));
}

void BasemgrImpl::Shutdown() {
  // Prevent the shutdown sequence from running twice.
  if (state_ == State::SHUTTING_DOWN) {
    return;
  }

  state_ = State::SHUTTING_DOWN;

  FXL_DLOG(INFO) << "fuchsia::modular::BaseShellContext::Shutdown()";

  if (settings_.test) {
    FXL_LOG(INFO)
        << std::endl
        << "============================================================"
        << std::endl
        << "======================== [" << settings_.test_name << "] Done";
  }

  // TODO(mesch): Some of these could be done in parallel too.
  // fuchsia::modular::UserProvider must go first, but the order after user
  // provider is for now rather arbitrary. We terminate base shell last so
  // that in tests testing::Teardown() is invoked at the latest possible time.
  // Right now it just demonstrates that AppTerminate() works as we like it
  // to.
  user_provider_impl_.Teardown(kUserProviderTimeout, [this] {
    FXL_DLOG(INFO) << "- fuchsia::modular::UserProvider down";
    StopTokenManagerFactoryApp()->Then([this] {
      FXL_DLOG(INFO) << "- fuchsia::auth::TokenManagerFactory down";
      StopBaseShell()->Then([this] {
        FXL_LOG(INFO) << "Clean Shutdown";
        on_shutdown_();
      });
    });
  });
}

void BasemgrImpl::GetAuthenticationUIContext(
    fidl::InterfaceRequest<fuchsia::auth::AuthenticationUIContext> request) {
  // TODO(MI4-1107): Basemgr needs to implement AuthenticationUIContext
  // itself, and proxy calls for StartOverlay & StopOverlay to BaseShell,
  // starting it if it's not running yet.
  FXL_CHECK(base_shell_);
  base_shell_->GetAuthenticationUIContext(std::move(request));
}

void BasemgrImpl::DidLogin() {
  // Continues if `enable_presenter` is set to true during testing, as
  // ownership of the Presenter should still be moved to the session shell.
  if (settings_.test && !settings_.enable_presenter) {
    // TODO(MI4-1117): Integration tests currently expect base shell to
    // always be running. So, if we're running under a test, do not shut down
    // the base shell after login.
    return;
  }

  // TODO(MI4-1117): See above. The base shell shouldn't be shut down.
  if (!settings_.test) {
    FXL_DLOG(INFO) << "Stopping base shell due to login";
    StopBaseShell();
  }

  InitializePresentation(session_shell_view_owner_);
}

void BasemgrImpl::DidLogout() {
  if (settings_.test) {
    // TODO(MI4-1117): Integration tests currently expect base shell to
    // always be running. So, if we're running under a test, DidLogin() will
    // not shut down the base shell after login; thus this method doesn't
    // need to re-start the base shell after a logout.
    return;
  }

  FXL_DLOG(INFO) << "Re-starting base shell due to logout";

  StartBaseShell();
}

fidl::InterfaceRequest<fuchsia::ui::viewsv1token::ViewOwner>
BasemgrImpl::GetSessionShellViewOwner(
    fidl::InterfaceRequest<fuchsia::ui::viewsv1token::ViewOwner>) {
  return session_shell_view_owner_.is_bound()
             ? session_shell_view_owner_.Unbind().NewRequest()
             : session_shell_view_owner_.NewRequest();
}

fidl::InterfaceHandle<fuchsia::sys::ServiceProvider>
BasemgrImpl::GetSessionShellServiceProvider(
    fidl::InterfaceHandle<fuchsia::sys::ServiceProvider>) {
  fidl::InterfaceHandle<fuchsia::sys::ServiceProvider> handle;
  service_namespace_.AddBinding(handle.NewRequest());
  return handle;
}

void BasemgrImpl::OnEvent(fuchsia::ui::input::KeyboardEvent event) {
  switch (event.code_point) {
    case ' ': {
      SwapSessionShell();
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
      FXL_DLOG(INFO) << "Unknown keyboard event: codepoint=" << event.code_point
                     << ", modifiers=" << event.modifiers;
      break;
  }
}

void BasemgrImpl::AddGlobalKeyboardShortcuts(
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

void BasemgrImpl::SwapSessionShell() {
  if (session_shell_settings_.empty()) {
    FXL_DLOG(INFO) << "No session shells has been defined";
    return;
  }
  auto shell_count = session_shell_settings_.size();
  if (shell_count <= 1) {
    FXL_DLOG(INFO)
        << "Only one session shell has been defined so switch is disabled";
    return;
  }
  active_session_shell_settings_index_ =
      (active_session_shell_settings_index_ + 1) % shell_count;

  UpdateSessionShellConfig();

  user_provider_impl_->SwapSessionShell(CloneStruct(session_shell_config_))
      ->Then([] { FXL_DLOG(INFO) << "Swapped session shell"; });
}

void BasemgrImpl::SetNextShadowTechnique() {
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

  SetShadowTechnique(
      next_shadow_technique(presentation_state_.shadow_technique));
}

void BasemgrImpl::SetShadowTechnique(
    fuchsia::ui::gfx::ShadowTechnique shadow_technique) {
  if (!presentation_state_.presentation)
    return;

  presentation_state_.shadow_technique = shadow_technique;

  FXL_LOG(INFO) << "Setting shadow technique to "
                << fidl::ToUnderlying(presentation_state_.shadow_technique);

  fuchsia::ui::gfx::RendererParam param;
  param.set_shadow_technique(presentation_state_.shadow_technique);

  std::vector<fuchsia::ui::gfx::RendererParam> renderer_params;
  renderer_params.push_back(std::move(param));

  presentation_state_.presentation->SetRendererParams(
      std::move(renderer_params));
}

void BasemgrImpl::ToggleClipping() {
  if (!presentation_state_.presentation)
    return;

  FXL_DLOG(INFO) << "Toggling clipping";

  presentation_state_.clipping_enabled = !presentation_state_.clipping_enabled;
  presentation_state_.presentation->EnableClipping(
      presentation_state_.clipping_enabled);
}

void BasemgrImpl::UpdateSessionShellConfig() {
  // The session shell settings overrides the session_shell flag passed via
  // command line, except in integration tests. TODO(MF-113): Consolidate
  // the session shell settings.
  fuchsia::modular::AppConfig session_shell_config;
  if (settings_.test || session_shell_settings_.empty()) {
    session_shell_config = CloneStruct(settings_.session_shell);
  } else {
    const auto& settings =
        session_shell_settings_[active_session_shell_settings_index_];
    session_shell_config.url = settings.name;
  }

  session_shell_config_ = std::move(session_shell_config);
}

void BasemgrImpl::ShowSetupOrLogin() {
  auto show_setup_or_login = [this] {
    // If there are no session shell settings specified, default to showing
    // setup.
    if (active_session_shell_settings_index_ >=
        session_shell_settings_.size()) {
      StartBaseShell();
      return;
    }

    if (kAutoLoginToGuest) {
      user_provider_impl_->Login(fuchsia::modular::UserLoginParams());
    } else {
      user_provider_impl_->PreviousUsers(
          [this](std::vector<fuchsia::modular::auth::Account> accounts) {
            if (accounts.empty()) {
              StartBaseShell();
            } else {
              fuchsia::modular::UserLoginParams params;
              params.account_id = accounts.at(0).id;
              user_provider_impl_->Login(std::move(params));
            }
          });
    }
  };

  // TODO(MF-134): Improve the factory reset logic by deleting more than just
  // the user data.
  // If the device needs factory reset, remove all the users before proceeding
  // with setup.
  device_settings_manager_.set_error_handler(
      [show_setup_or_login](zx_status_t status) { show_setup_or_login(); });
  device_settings_manager_->GetInteger(
      kFactoryResetKey,
      [this, show_setup_or_login](int factory_reset_value,
                                  fuchsia::devicesettings::Status status) {
        if (status == fuchsia::devicesettings::Status::ok &&
            factory_reset_value > 0) {
          // Unset the factory reset flag.
          device_settings_manager_->SetInteger(
              kFactoryResetKey, 0, [](bool result) {
                if (!result) {
                  FXL_LOG(WARNING) << "Factory reset flag was not updated.";
                }
              });

          user_provider_impl_->PreviousUsers(
              [this](std::vector<fuchsia::modular::auth::Account> accounts) {
                std::vector<FuturePtr<>> did_remove_users;
                did_remove_users.reserve(accounts.size());

                for (const auto& account : accounts) {
                  auto did_remove_user = Future<>::Create(
                      "BasemgrImpl.ShowSetupOrLogin.did_remove_user");
                  user_provider_impl_->RemoveUser(
                      account.id,
                      [did_remove_user](fidl::StringPtr error_code) {
                        if (error_code) {
                          FXL_LOG(WARNING) << "Account was not removed during "
                                              "factory reset. Error code: "
                                           << error_code;
                        }
                        did_remove_user->Complete();
                      });
                  did_remove_users.emplace_back(did_remove_user);
                }

                Wait("BasemgrImpl.ShowSetupOrLogin.Wait", did_remove_users)
                    ->Then([this] { StartBaseShell(); });
              });
        } else {
          show_setup_or_login();
        }
      });
}

void BasemgrImpl::RestartSession(RestartSessionCallback on_restart_complete) {
  user_provider_impl_->RestartSession(on_restart_complete);
}

void BasemgrImpl::LoginAsGuest() {
  fuchsia::modular::UserLoginParams params;
  user_provider_impl_->Login(std::move(params));
}

}  // namespace modular
