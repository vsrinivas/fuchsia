// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/basemgr/session_context_impl.h"

#include <fcntl.h>
#include <lib/fidl/cpp/synchronous_interface_ptr.h>
#include <lib/fsl/io/fd.h>

#include <memory>
#include <string>
#include <utility>

#include "peridot/lib/common/async_holder.h"
#include "peridot/lib/common/teardown.h"
#include "peridot/lib/fidl/array_to_string.h"
#include "peridot/lib/modular_config/modular_config_constants.h"
#include "src/lib/files/file.h"
#include "src/lib/files/unique_fd.h"

namespace modular {

SessionContextImpl::SessionContextImpl(
    fuchsia::sys::Launcher* const launcher, std::string session_id,
    fuchsia::modular::AppConfig sessionmgr_config,
    fuchsia::modular::AppConfig session_shell_config,
    fuchsia::modular::AppConfig story_shell_config,
    bool use_session_shell_for_story_shell_factory,
    fidl::InterfaceHandle<fuchsia::auth::TokenManager> ledger_token_manager,
    fidl::InterfaceHandle<fuchsia::auth::TokenManager> agent_token_manager,
    fuchsia::modular::auth::AccountPtr account,
    fuchsia::ui::views::ViewToken view_token,
    GetPresentationCallback get_presentation,
    OnSessionShutdownCallback on_session_shutdown)
    : session_context_binding_(this),
      get_presentation_(std::move(get_presentation)),
      on_session_shutdown_(std::move(on_session_shutdown)) {
  FXL_CHECK(get_presentation_);
  FXL_CHECK(on_session_shutdown_);

  // TODO(MF-280): We should replace USER* with SESSION* below. However, this
  // will lose existing user data, so the timing needs to be considered.
  // 0. Generate the path to map '/data' for the sessionmgr we are starting.
  std::string data_origin = std::string("/data/modular/USER_") + session_id;

  FXL_LOG(INFO) << "SESSIONMGR DATA ORIGIN IS " << data_origin;

  // 1. Create a PseudoDir containing startup.config if basemgr is reading
  // configurations from file. This directory will be injected into sessionmgr's
  // namespace and sessionmgr will read its configurations from there.
  auto flat_namespace = MakeConfigNamespace();

  // 2. Launch Sessionmgr in the current environment.
  sessionmgr_app_ = std::make_unique<AppClient<fuchsia::modular::Lifecycle>>(
      launcher, std::move(sessionmgr_config), data_origin,
      /* additional_services= */ nullptr, std::move(flat_namespace));

  // 3. Initialize the Sessionmgr service.
  sessionmgr_app_->services().ConnectToService(sessionmgr_.NewRequest());
  sessionmgr_->Initialize(
      session_id, std::move(account), std::move(session_shell_config),
      std::move(story_shell_config), use_session_shell_for_story_shell_factory,
      std::move(ledger_token_manager), std::move(agent_token_manager),
      session_context_binding_.NewBinding(), std::move(view_token));

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

fuchsia::sys::FlatNamespacePtr SessionContextImpl::MakeConfigNamespace() {
  // Determine where basemgr is reading configs from
  std::string config_dir = "";
  if (files::IsFile(modular_config::kOverridenStartupConfigPath)) {
    config_dir = modular_config::kOverridenConfigDirPath;
  } else if (files::IsFile(modular_config::kStartupConfigPath)) {
    config_dir = modular_config::kStartupConfigDirPath;
  }

  // Clone basemgr's config directory if it exists
  if (!config_dir.empty()) {
    fxl::UniqueFD dir(open(config_dir.c_str(), O_DIRECTORY | O_RDONLY));
    auto flat_namespace = fuchsia::sys::FlatNamespace::New();
    flat_namespace->paths.push_back(modular_config::kOverridenConfigDirPath);
    flat_namespace->directories.push_back(
        fsl::CloneChannelFromFileDescriptor(dir.get()));
    return flat_namespace;
  }
  return nullptr;
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
  get_presentation_(std::move(request));
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

void SessionContextImpl::Shutdown() {
  Shutdown(/* logout_users= */ false, [] {});
}

}  // namespace modular
