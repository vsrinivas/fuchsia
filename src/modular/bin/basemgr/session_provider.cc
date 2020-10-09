// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/modular/bin/basemgr/session_provider.h"

#include <fuchsia/hardware/power/statecontrol/cpp/fidl.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/zx/clock.h>
#include <zircon/status.h>

#include "src/lib/intl/intl_property_provider_impl/intl_property_provider_impl.h"
#include "src/modular/lib/fidl/clone.h"
#include "src/modular/lib/modular_config/modular_config.h"
#include "src/modular/lib/modular_config/modular_config_constants.h"

namespace modular {

using intl::IntlPropertyProviderImpl;
using ShutDownReason = SessionContextImpl::ShutDownReason;

static constexpr auto kMaxCrashRecoveryLimit = 3;

static constexpr auto kMaxCrashRecoveryDuration = zx::hour(1);

SessionProvider::SessionProvider(Delegate* const delegate, fuchsia::sys::Launcher* const launcher,
                                 fuchsia::hardware::power::statecontrol::Admin* const administrator,
                                 const modular::ModularConfigAccessor* const config_accessor,
                                 IntlPropertyProviderImpl* const intl_property_provider,
                                 fuchsia::sys::ServiceList services_from_session_launcher,
                                 fit::function<void()> on_zero_sessions)
    : delegate_(delegate),
      launcher_(launcher),
      administrator_(administrator),
      config_accessor_(config_accessor),
      intl_property_provider_(intl_property_provider),
      on_zero_sessions_(std::move(on_zero_sessions)),
      session_launcher_service_names_(std::move(services_from_session_launcher.names)),
      session_launcher_service_dir_(std::move(services_from_session_launcher.host_directory)) {
  last_crash_time_ = zx::clock::get_monotonic();
  // Bind `fuchsia.intl.PropertyProvider` to the implementation instance owned by this class.
  sessionmgr_service_dir_.AddEntry(
      fuchsia::intl::PropertyProvider::Name_,
      std::make_unique<vfs::Service>(intl_property_provider_->GetHandler()));
}

SessionProvider::StartSessionResult SessionProvider::StartSession(
    fuchsia::ui::views::ViewToken view_token) {
  if (is_session_running()) {
    FX_LOGS(WARNING) << "StartSession() called when session context already "
                        "exists. Try calling SessionProvider::Teardown()";
    return fit::error(ZX_ERR_BAD_STATE);
  }

  auto services = CreateAndServeSessionmgrServices();

  auto done = [this](SessionContextImpl::ShutDownReason shutdown_reason) {
    OnSessionShutdown(shutdown_reason);
  };

  fuchsia::modular::session::AppConfig sessionmgr_app_config;
  sessionmgr_app_config.set_url(modular_config::kSessionmgrUrl);

  // Session context initializes and holds the sessionmgr process.
  fuchsia::sys::ServiceList services_from_session_launcher;
  services_from_session_launcher.names = session_launcher_service_names_;
  services_from_session_launcher.host_directory =
      session_launcher_service_dir_.CloneChannel().TakeChannel();
  session_context_ = std::make_unique<SessionContextImpl>(
      launcher_, std::move(sessionmgr_app_config), config_accessor_, std::move(view_token),
      std::move(services),
      std::move(services_from_session_launcher),
      /* get_presentation= */
      [this](fidl::InterfaceRequest<fuchsia::ui::policy::Presentation> request) {
        delegate_->GetPresentation(std::move(request));
      },
      done);

  return fit::ok();
}

void SessionProvider::Teardown(fit::function<void()> callback) {
  if (!is_session_running()) {
    callback();
    return;
  }

  // Shutdown will execute the given |callback|, then destroy |session_context_|.
  session_context_->Shutdown(ShutDownReason::CRITICAL_FAILURE, std::move(callback));
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

fuchsia::sys::ServiceListPtr SessionProvider::CreateAndServeSessionmgrServices() {
  fidl::InterfaceHandle<fuchsia::io::Directory> dir_handle;
  sessionmgr_service_dir_.Serve(fuchsia::io::OPEN_RIGHT_READABLE | fuchsia::io::OPEN_RIGHT_WRITABLE,
                                dir_handle.NewRequest().TakeChannel());

  auto services = fuchsia::sys::ServiceList::New();
  services->names.push_back(fuchsia::intl::PropertyProvider::Name_);
  services->host_directory = dir_handle.TakeChannel();

  return services;
}

}  // namespace modular
