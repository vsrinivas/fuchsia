// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/modular/bin/basemgr/session_provider.h"

#include <fuchsia/hardware/power/statecontrol/cpp/fidl.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/vfs/cpp/remote_dir.h>
#include <lib/zx/clock.h>
#include <zircon/errors.h>
#include <zircon/status.h>

#include "src/modular/lib/fidl/clone.h"
#include "src/modular/lib/modular_config/modular_config.h"
#include "src/modular/lib/modular_config/modular_config_constants.h"

namespace modular {

using ShutDownReason = SessionContextImpl::ShutDownReason;

static constexpr auto kMaxCrashRecoveryLimit = 3;

static constexpr auto kMaxCrashRecoveryDuration = zx::hour(1);

SessionProvider::SessionProvider(Delegate* const delegate, fuchsia::sys::Launcher* const launcher,
                                 fuchsia::hardware::power::statecontrol::Admin* const administrator,
                                 const modular::ModularConfigAccessor* const config_accessor,
                                 fuchsia::sys::ServiceList v2_services_for_sessionmgr,
                                 vfs::PseudoDir* outgoing_dir_root,
                                 fit::function<void()> on_zero_sessions)
    : delegate_(delegate),
      launcher_(launcher),
      administrator_(administrator),
      config_accessor_(config_accessor),
      on_zero_sessions_(std::move(on_zero_sessions)),
      v2_services_for_sessionmgr_names_(std::move(v2_services_for_sessionmgr.names)),
      v2_services_for_sessionmgr_dir_(std::move(v2_services_for_sessionmgr.host_directory)),
      outgoing_dir_root_(outgoing_dir_root) {
  last_crash_time_ = zx::clock::get_monotonic();
}

SessionProvider::StartSessionResult SessionProvider::StartSession(
    fuchsia::ui::views::ViewToken view_token, scenic::ViewRefPair view_ref_pair) {
  if (is_session_running()) {
    FX_LOGS(WARNING) << "StartSession() called when session context already "
                        "exists. Try calling SessionProvider::Teardown()";
    return fpromise::error(ZX_ERR_BAD_STATE);
  }

  fuchsia::modular::session::AppConfig sessionmgr_app_config;
  sessionmgr_app_config.set_url(modular_config::kSessionmgrUrl);

  // Session context initializes and holds the sessionmgr process.
  fuchsia::sys::ServiceList v2_services_for_sessionmgr;
  v2_services_for_sessionmgr.names = v2_services_for_sessionmgr_names_;
  v2_services_for_sessionmgr.host_directory =
      v2_services_for_sessionmgr_dir_.CloneChannel().TakeChannel();

  fuchsia::io::DirectoryPtr svc_from_v1_sessionmgr_dir_ptr;
  auto svc_from_v1_sessionmgr_dir_request = svc_from_v1_sessionmgr_dir_ptr.NewRequest();

  auto path = modular_config::kServicesFromV1Sessionmgr;
  zx_status_t status;
  FX_LOGS(INFO) << "(Re-)adding subdir " << path << " to the outgoing root dir";
  status = outgoing_dir_root_->RemoveEntry(path);
  if (status != ZX_OK && status != ZX_ERR_NOT_FOUND) {
    FX_PLOGS(FATAL, status) << "Failed to remove previous instance of remote_dir from "
                               "basemgr's outgoing directory, for path: /"
                            << path;
  }
  status = outgoing_dir_root_->AddEntry(
      path, std::make_unique<vfs::RemoteDir>(std::move(svc_from_v1_sessionmgr_dir_ptr)));
  if (status != ZX_OK) {
    FX_PLOGS(FATAL, status)
        << "Failed to add remote_dir to basemgr's outgoing directory, for path: /" << path;
  }

  session_context_ = std::make_unique<SessionContextImpl>(
      launcher_, std::move(sessionmgr_app_config), config_accessor_, std::move(view_token),
      std::move(view_ref_pair), std::move(v2_services_for_sessionmgr),
      std::move(svc_from_v1_sessionmgr_dir_request),
      /*get_presentation=*/
      [this](fidl::InterfaceRequest<fuchsia::ui::policy::Presentation> request) {
        delegate_->GetPresentation(std::move(request));
      },
      /*on_session_shutdown=*/
      [this](SessionContextImpl::ShutDownReason shutdown_reason) {
        OnSessionShutdown(shutdown_reason);
      });

  return fpromise::ok();
}

void SessionProvider::Teardown(fit::function<void()> callback) {
  if (!is_session_running()) {
    callback();
    return;
  }

  // Shutdown will execute the given |callback|, then destroy |session_context_|.
  session_context_->Shutdown(ShutDownReason::CLIENT_REQUEST, std::move(callback));
}

void SessionProvider::RestartSession(fit::function<void()> on_restart_complete) {
  if (!is_session_running()) {
    return;
  }

  // Shutting down a session effectively restarts the session.
  session_context_->Shutdown(ShutDownReason::CLIENT_REQUEST, std::move(on_restart_complete));
}

void SessionProvider::OnSessionShutdown(SessionContextImpl::ShutDownReason shutdown_reason) {
  if (shutdown_reason == SessionContextImpl::ShutDownReason::CRITICAL_FAILURE) {
    if (session_crash_recovery_counter_ != 0) {
      zx::duration duration = zx::clock::get_monotonic() - last_crash_time_;
      // If last retry is 1 hour ago, the counter will be reset
      if (duration > kMaxCrashRecoveryDuration) {
        session_crash_recovery_counter_ = 1;
      }
    }

    // Check if max retry limit is reached
    if (session_crash_recovery_counter_ == kMaxCrashRecoveryLimit) {
      FX_LOGS(ERROR) << "Sessionmgr restart limit reached. Considering "
                        "this an unrecoverable failure.";
      administrator_->Reboot(
          fuchsia::hardware::power::statecontrol::RebootReason::SESSION_FAILURE,
          [](fuchsia::hardware::power::statecontrol::Admin_Reboot_Result status) {
            if (status.is_err()) {
              FX_PLOGS(FATAL, status.err()) << "Failed to reboot";
            }
          });
      return;
    }
    session_crash_recovery_counter_ += 1;
    last_crash_time_ = zx::clock::get_monotonic();
  }

  auto delete_session_context = [this] {
    session_context_.reset();
    on_zero_sessions_();
  };

  delete_session_context();
}

}  // namespace modular
