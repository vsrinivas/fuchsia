// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/basemgr/session_context_impl.h"

#include <memory>
#include <utility>

#include <lib/fidl/cpp/synchronous_interface_ptr.h>

#include "peridot/lib/common/async_holder.h"
#include "peridot/lib/common/teardown.h"
#include "peridot/lib/fidl/array_to_string.h"

namespace modular {

namespace {
// The service name of the Presentation service that is routed between
// BaseShell and SessionShell. The same service exchange between SessionShell
// and StoryShell uses the SessionShellPresentationProvider service, which is
// discoverable.
// NOTE: This is defined in basemgr_impl.cc as well.
// TODO(SCN-595): mozart.Presentation is being renamed to ui.Presenter.
constexpr char kPresentationService[] = "mozart.Presentation";
}  // namespace

SessionContextImpl::SessionContextImpl(
    fuchsia::sys::Launcher* const launcher,
    fuchsia::modular::AppConfig sessionmgr_config,
    fuchsia::modular::AppConfig session_shell_config,
    fuchsia::modular::AppConfig story_shell_config,
    bool use_session_shell_for_story_shell_factory,
    fidl::InterfaceHandle<fuchsia::auth::TokenManager> ledger_token_manager,
    fidl::InterfaceHandle<fuchsia::auth::TokenManager> agent_token_manager,
    fuchsia::modular::auth::AccountPtr account,
    fidl::InterfaceRequest<fuchsia::ui::viewsv1token::ViewOwner>
        view_owner_request,
    fidl::InterfaceHandle<fuchsia::sys::ServiceProvider> base_shell_services,
    OnSessionShutdownCallback on_session_shutdown)
    : session_context_binding_(this),
      base_shell_services_(base_shell_services ? base_shell_services.Bind()
                                               : nullptr),
      on_session_shutdown_(std::move(on_session_shutdown)) {
  // 0. Generate the path to map '/data' for the sessionmgr we are starting.
  std::string data_origin;
  if (!account) {
    // Guest user.
    // Generate a random number to be used in this case.
    uint32_t random_number = 0;
    zx_cprng_draw(&random_number, sizeof random_number);
    data_origin = std::string("/data/modular/USER_GUEST_") +
                  std::to_string(random_number);
  } else {
    // Non-guest user.
    data_origin = std::string("/data/modular/USER_") + std::string(account->id);
  }

  FXL_LOG(INFO) << "SESSIONMGR DATA ORIGIN IS " << data_origin;

  // 1. Launch Sessionmgr in the current environment.
  sessionmgr_app_ = std::make_unique<AppClient<fuchsia::modular::Lifecycle>>(
      launcher, std::move(sessionmgr_config), data_origin);

  // 2. Initialize the Sessionmgr service.
  sessionmgr_app_->services().ConnectToService(sessionmgr_.NewRequest());
  sessionmgr_->Initialize(
      std::move(account), std::move(session_shell_config),
      std::move(story_shell_config), use_session_shell_for_story_shell_factory,
      std::move(ledger_token_manager), std::move(agent_token_manager),
      session_context_binding_.NewBinding(), std::move(view_owner_request));

  sessionmgr_app_->SetAppErrorHandler([this] {
    FXL_LOG(ERROR) << "Sessionmgr seems to have crashed unexpectedly. "
                   << "Calling on_session_shutdown_().";
    // This prevents us from receiving any further requests.
    session_context_binding_.Unbind();

    // Shutdown(), which expects a graceful shutdown of sessionmgr, does not
    // apply here because sessionmgr crashed. Just run |on_session_shutdown_|
    // directly.
    on_session_shutdown_(/* logout_users= */ false);
  });
}

// TODO(MF-120): Replace method in favor of letting sessionmgr launch base
// shell via SessionUserProvider.
void SessionContextImpl::Shutdown(bool logout_users,
                                  fit::function<void()> callback) {
  shutdown_callbacks_.push_back(std::move(callback));
  if (shutdown_callbacks_.size() > 1) {
    FXL_LOG(INFO)
        << "fuchsia::modular::internal::SessionContext::Shutdown() "
           "already called, queuing callback while shutdown is in progress.";
    return;
  }

  // This should prevent us from receiving any further requests.
  session_context_binding_.Unbind();

  sessionmgr_app_->Teardown(kSessionmgrTimeout, [this, logout_users] {
    for (const auto& callback : shutdown_callbacks_) {
      callback();
    }

    on_session_shutdown_(logout_users);
  });
}

void SessionContextImpl::GetPresentation(
    fidl::InterfaceRequest<fuchsia::ui::policy::Presentation> request) {
  if (base_shell_services_) {
    base_shell_services_->ConnectToService(kPresentationService,
                                           request.TakeChannel());
  }
}

FuturePtr<> SessionContextImpl::SwapSessionShell(
    fuchsia::modular::AppConfig session_shell_config) {
  auto future = Future<>::Create("SwapSessionShell");
  sessionmgr_->SwapSessionShell(std::move(session_shell_config),
                                future->Completer());
  return future;
}

void SessionContextImpl::Logout() {
  Shutdown(/* logout_users= */ true, [] {});
}

}  // namespace modular
