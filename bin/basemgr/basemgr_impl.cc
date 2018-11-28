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
#include "peridot/bin/basemgr/user_provider_impl.h"
#include "peridot/lib/common/async_holder.h"
#include "peridot/lib/common/names.h"
#include "peridot/lib/common/teardown.h"
#include "peridot/lib/fidl/app_client.h"
#include "peridot/lib/fidl/clone.h"
#include "peridot/lib/session_shell_settings/session_shell_settings.h"
#include "peridot/lib/util/filesystem.h"

namespace modular {

BasemgrImpl::BasemgrImpl(
    const modular::BasemgrSettings& settings,
    std::shared_ptr<component::StartupContext> const context,
    std::function<void()> on_shutdown)
    : settings_(settings),
      user_provider_impl_("UserProviderImpl"),
      context_(std::move(context)),
      on_shutdown_(std::move(on_shutdown)),
      base_shell_context_binding_(this),
      account_provider_context_binding_(this),
      authentication_context_provider_binding_(this) {
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

  monitor_.set_error_handler([](zx_status_t status) {
    FXL_LOG(ERROR) << "No basemgr monitor found.";
    exit(1);
  });

  monitor_->GetConnectionCount([this](uint32_t count) {
    if (count != 1) {
      FXL_LOG(ERROR) << "Another basemgr is running."
                     << " Please use that one, or shut it down first.";
      exit(1);
    }

    Start();
  });
}

BasemgrImpl::~BasemgrImpl() = default;

void BasemgrImpl::InitializePresentation(
    fidl::InterfaceHandle<fuchsia::ui::viewsv1token::ViewOwner> view_owner) {
  if (settings_.test && !settings_.enable_presenter) {
    return;
  }

  auto presentation_request =
      presentation_state_.presentation.is_bound()
          ? presentation_state_.presentation.Unbind().NewRequest()
          : presentation_state_.presentation.NewRequest();

  context_->ConnectToEnvironmentService<fuchsia::ui::policy::Presenter>()
      ->Present2(zx::eventpair(view_owner.TakeChannel().release()),
                 std::move(presentation_request));

  AddGlobalKeyboardShortcuts(presentation_state_.presentation);

  SetShadowTechnique(presentation_state_.shadow_technique);
}

void BasemgrImpl::StartBaseShell() {
  if (base_shell_running_) {
    FXL_DLOG(INFO) << "StartBaseShell() called when already running";

    return;
  }

  base_shell_app_ = std::make_unique<AppClient<fuchsia::modular::Lifecycle>>(
      context_->launcher().get(), CloneStruct(settings_.base_shell));
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

FuturePtr<> BasemgrImpl::StopAccountProvider() {
  if (!account_provider_) {
    FXL_DLOG(INFO) << "StopAccountProvider() called when already stopped";

    return Future<>::CreateCompleted("StopAccountProvider::Completed");
  }

  auto did_stop = Future<>::Create("StopAccountProvider");

  account_provider_->Teardown(kBasicTimeout, [did_stop, this] {
    FXL_DLOG(INFO) << "- fuchsia::modular::auth::AccountProvider down";

    account_provider_.release();
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

  // Start the base shell. This is done first so that we can show some UI
  // until other things come up.
  StartBaseShell();

  // Wait for persistent data to come up.
  if (!settings_.no_minfs) {
    WaitForMinfs();
  }

  // Start OAuth Token Manager App.
  fuchsia::modular::AppConfig token_manager_config;
  if (settings_.enable_garnet_token_manager) {
    token_manager_config.url = "token_manager_factory";
    FXL_DLOG(INFO) << "Initialzing token_manager_factory_app()";
    token_manager_factory_app_ =
        std::make_unique<AppClient<fuchsia::modular::Lifecycle>>(
            context_->launcher().get(), CloneStruct(token_manager_config));
    token_manager_factory_app_->services().ConnectToService(
        token_manager_factory_.NewRequest());
  } else {
    token_manager_config.url = settings_.account_provider.url;
    token_manager_factory_app_.release();
  }

  account_provider_ =
      std::make_unique<AppClient<fuchsia::modular::auth::AccountProvider>>(
          context_->launcher().get(), std::move(token_manager_config),
          "/data/modular/ACCOUNT_MANAGER");
  account_provider_->SetAppErrorHandler(
      [] { FXL_CHECK(false) << "Token manager crashed. Stopping basemgr."; });
  account_provider_->primary_service()->Initialize(
      account_provider_context_binding_.NewBinding());

  user_provider_impl_.reset(new UserProviderImpl(
      context_, settings_.sessionmgr, settings_.session_shell,
      settings_.story_shell, account_provider_->primary_service().get(),
      token_manager_factory_.get(),
      authentication_context_provider_binding_.NewBinding().Bind(),
      settings_.enable_garnet_token_manager, this));

  ReportEvent(ModularEvent::BOOTED_TO_BASEMGR);
}

void BasemgrImpl::GetUserProvider(
    fidl::InterfaceRequest<fuchsia::modular::UserProvider> request) {
  user_provider_impl_->Connect(std::move(request));
}

void BasemgrImpl::Shutdown() {
  // TODO(mesch): Some of these could be done in parallel too.
  // fuchsia::modular::UserProvider must go first, but the order after user
  // provider is for now rather arbitrary. We terminate base shell last so
  // that in tests testing::Teardown() is invoked at the latest possible time.
  // Right now it just demonstrates that AppTerminate() works as we like it
  // to.
  FXL_DLOG(INFO) << "fuchsia::modular::BaseShellContext::Shutdown()";

  if (settings_.test) {
    FXL_LOG(INFO)
        << std::endl
        << "============================================================"
        << std::endl
        << "======================== [" << settings_.test_name << "] Done";
  }

  user_provider_impl_.Teardown(kUserProviderTimeout, [this] {
    FXL_DLOG(INFO) << "- fuchsia::modular::UserProvider down";
    StopAccountProvider()->Then([this] {
      FXL_DLOG(INFO) << "- fuchsia::modular::auth::AccountProvider down";
      StopTokenManagerFactoryApp()->Then([this] {
        FXL_DLOG(INFO) << "- fuchsia::auth::TokenManagerFactory down";
        StopBaseShell()->Then([this] {
          FXL_LOG(INFO) << "Clean Shutdown";
          on_shutdown_();
        });
      });
    });
  });
}

void BasemgrImpl::GetAuthenticationContext(
    fidl::StringPtr account_id,
    fidl::InterfaceRequest<fuchsia::modular::auth::AuthenticationContext>
        request) {
  // TODO(MI4-1107): Basemgr needs to implement AuthenticationContext
  // itself, and proxy calls for StartOverlay & StopOverlay to BaseShell,
  // starting it if it's not running yet.
  FXL_CHECK(base_shell_);
  base_shell_->GetAuthenticationContext(account_id, std::move(request));
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

  const auto& settings_vector = SessionShellSettings::GetSystemSettings();
  if (active_session_shell_index_ >= settings_vector.size()) {
    FXL_LOG(ERROR) << "Active session shell index is "
                   << active_session_shell_index_ << ", but only "
                   << settings_vector.size() << " session shells exist.";
    return;
  }

  UpdatePresentation(settings_vector[active_session_shell_index_]);
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

void BasemgrImpl::UpdatePresentation(const SessionShellSettings& settings) {
  if (settings.display_usage != fuchsia::ui::policy::DisplayUsage::kUnknown) {
    FXL_DLOG(INFO) << "Setting display usage: "
                   << fidl::ToUnderlying(settings.display_usage);
    presentation_state_.presentation->SetDisplayUsage(settings.display_usage);
  }

  if (!std::isnan(settings.screen_width) &&
      !std::isnan(settings.screen_height)) {
    FXL_DLOG(INFO) << "Setting display size: " << settings.screen_width << " x "
                   << settings.screen_height;
    presentation_state_.presentation->SetDisplaySizeInMm(
        settings.screen_width, settings.screen_height);
  }
}

void BasemgrImpl::SwapSessionShell() {
  if (SessionShellSettings::GetSystemSettings().empty()) {
    FXL_DLOG(INFO) << "No session shells has been defined";
    return;
  }

  auto shell_count = SessionShellSettings::GetSystemSettings().size();

  if (shell_count <= 1) {
    FXL_DLOG(INFO) << "Only one session shell has been defined so switch is disabled";
    return;
  }

  active_session_shell_index_ =
      (active_session_shell_index_ + 1) %
      shell_count;
  const auto& settings =
      SessionShellSettings::GetSystemSettings().at(active_session_shell_index_);

  auto session_shell_config = fuchsia::modular::AppConfig::New();
  session_shell_config->url = settings.name;

  user_provider_impl_->SwapSessionShell(std::move(*session_shell_config))
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

  auto renderer_params =
      fidl::VectorPtr<fuchsia::ui::gfx::RendererParam>::New(0);
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

}  // namespace modular
