// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/modular/bin/basemgr/session_context_impl.h"

#include <lib/syslog/cpp/macros.h>

#include "src/modular/bin/basemgr/sessions.h"
#include "src/modular/lib/common/async_holder.h"
#include "src/modular/lib/common/teardown.h"
#include "src/modular/lib/modular_config/modular_config_constants.h"
#include "src/modular/lib/pseudo_dir/pseudo_dir_utils.h"

namespace modular {

SessionContextImpl::SessionContextImpl(
    fuchsia::sys::Launcher* const launcher, fuchsia::sys::Environment* const base_environment,
    fuchsia::modular::session::AppConfig sessionmgr_app_config,
    const modular::ModularConfigAccessor* const config_accessor,
    fuchsia::ui::views::ViewToken view_token,
    fuchsia::sys::ServiceListPtr additional_services_for_sessionmgr,
    fuchsia::sys::ServiceList additional_services_for_agents,
    GetPresentationCallback get_presentation, OnSessionShutdownCallback on_session_shutdown)
    : session_context_binding_(this),
      get_presentation_(std::move(get_presentation)),
      on_session_shutdown_(std::move(on_session_shutdown)),
      weak_factory_(this) {
  FX_CHECK(get_presentation_);
  FX_CHECK(on_session_shutdown_);

  // Delete all existing sessions that have been created with a random ID.
  // TODO(fxbug.dev/51752): Remove once there are no sessions with random IDs in use
  sessions::DeleteSessionsWithRandomIds(base_environment);

  // Determine an ID for the new session and report that it was created to Cobalt.
  const auto use_random_id = config_accessor->use_random_session_id();

  if (use_random_id) {
    FX_LOGS(WARNING) << "DEPRECATED! Starting session with random session ID.";
  } else {
    FX_LOGS(INFO) << "Starting session with stable session ID.";
  }

  std::string session_id =
      use_random_id ? sessions::GetRandomSessionId() : sessions::GetStableSessionId();

  sessions::ReportNewSessionToCobalt(session_id);

  // Generate the path to map '/data' for the sessionmgr we are starting
  auto data_origin = sessions::GetSessionDirectory(session_id);

  // Create a PseudoDir containing startup.config. This directory will be injected into
  // sessionmgr's namespace and sessionmgr will read its configurations from there.
  auto config_namespace = CreateAndServeConfigNamespace(config_accessor->GetConfigAsJsonString());

  // Launch Sessionmgr in the current environment.
  sessionmgr_app_ = std::make_unique<AppClient<fuchsia::modular::Lifecycle>>(
      launcher, std::move(sessionmgr_app_config), data_origin,
      std::move(additional_services_for_sessionmgr), std::move(config_namespace));

  // Initialize the Sessionmgr service.
  sessionmgr_app_->services().ConnectToService(sessionmgr_.NewRequest());
  sessionmgr_->Initialize(session_id, session_context_binding_.NewBinding(),
                          std::move(additional_services_for_agents), std::move(view_token));

  sessionmgr_app_->SetAppErrorHandler([weak_this = weak_factory_.GetWeakPtr()] {
    FX_LOGS(ERROR) << "Sessionmgr seems to have crashed unexpectedly. "
                   << "Calling on_session_shutdown_().";
    // This prevents us from receiving any further requests.
    weak_this->session_context_binding_.Unbind();

    // Shutdown(), which expects a graceful shutdown of sessionmgr, does not
    // apply here because sessionmgr crashed. Move |on_session_shutdown_| on to the stack before
    // invoking it, in case the |on_session_shutdown_| deletes |this|.
    auto on_session_shutdown = std::move(weak_this->on_session_shutdown_);
    on_session_shutdown(ShutDownReason::CRITICAL_FAILURE);
    // Don't touch |this|.
  });
}

fuchsia::sys::FlatNamespacePtr SessionContextImpl::CreateAndServeConfigNamespace(
    std::string config_contents) {
  zx::channel config_request_channel;
  zx::channel config_dir_channel;

  FX_CHECK(zx::channel::create(0u, &config_request_channel, &config_dir_channel) == ZX_OK);

  // Host the config file in a PseudoDir
  config_dir_ = modular::MakeFilePathWithContents(modular_config::kStartupConfigFilePath,
                                                  std::move(config_contents));
  config_dir_->Serve(fuchsia::io::OPEN_RIGHT_READABLE, std::move(config_request_channel));

  auto flat_namespace = fuchsia::sys::FlatNamespace::New();
  flat_namespace->paths.push_back(modular_config::kOverriddenConfigDir);
  flat_namespace->directories.push_back(std::move(config_dir_channel));

  return flat_namespace;
}

void SessionContextImpl::Shutdown(ShutDownReason reason, fit::function<void()> callback) {
  shutdown_callbacks_.push_back(std::move(callback));
  if (shutdown_callbacks_.size() > 1) {
    FX_LOGS(INFO) << "fuchsia::modular::internal::SessionContext::Shutdown() "
                     "already called, queuing callback while shutdown is in progress.";
    return;
  }

  // Close the SessionContext channel to ensure no more requests from the
  // channel are processed.
  session_context_binding_.Unbind();

  sessionmgr_app_->Teardown(kSessionmgrTimeout, [weak_this = weak_factory_.GetWeakPtr(), reason] {
    // One of the callbacks might delete |SessionContextImpl|, so always guard against
    // WeakPtr<SessionContextImpl>.
    for (const auto& callback : weak_this->shutdown_callbacks_) {
      callback();
      if (!weak_this) {
        return;
      }
    }
    auto on_session_shutdown = std::move(weak_this->on_session_shutdown_);
    on_session_shutdown(reason);
  });
}

void SessionContextImpl::GetPresentation(
    fidl::InterfaceRequest<fuchsia::ui::policy::Presentation> request) {
  get_presentation_(std::move(request));
}

void SessionContextImpl::Restart() {
  Shutdown(ShutDownReason::CLIENT_REQUEST, [] {});
}

void SessionContextImpl::RestartDueToCriticalFailure() {
  Shutdown(ShutDownReason::CRITICAL_FAILURE, [] {});
}

}  // namespace modular
