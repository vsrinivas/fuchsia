// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MODULAR_BIN_BASEMGR_SESSION_PROVIDER_H_
#define SRC_MODULAR_BIN_BASEMGR_SESSION_PROVIDER_H_

#include <fuchsia/hardware/power/statecontrol/cpp/fidl.h>
#include <fuchsia/modular/cpp/fidl.h>
#include <fuchsia/modular/session/cpp/fidl.h>
#include <fuchsia/ui/views/cpp/fidl.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/sys/cpp/service_directory.h>
#include <lib/ui/scenic/cpp/view_ref_pair.h>
#include <lib/vfs/cpp/pseudo_dir.h>

#include "src/modular/bin/basemgr/reboot_rate_limiter.h"
#include "src/modular/bin/basemgr/session_context_impl.h"
#include "src/modular/lib/async/cpp/future.h"
#include "src/modular/lib/modular_config/modular_config_accessor.h"

namespace modular {

// Default file path for tracking reboots. The file will contain a timestamp
// of the last reboot executed and a counter tracking all reboots.
constexpr char kRebootTrackerFilePath[] = "/data/modular-reboot-tracker.txt";

class SessionProvider {
 public:
  using StartSessionResult = fpromise::result<void, zx_status_t>;

  // Target constructor.
  //
  // |on_zero_sessions| is invoked when all sessions have been deleted. This is
  // meant to be a callback for BasemgrImpl to start a new session.
  SessionProvider(fuchsia::sys::Launcher* launcher,
                  fuchsia::hardware::power::statecontrol::Admin* administrator,
                  const modular::ModularConfigAccessor* config_accessor,
                  fuchsia::sys::ServiceList v2_services_for_sessionmgr,
                  vfs::PseudoDir* outgoing_dir_root, fit::function<void()> on_zero_sessions,
                  std::string reboot_tracker_file_path = kRebootTrackerFilePath);

  // Starts a new sessionmgr process if there isn't one already.
  //
  // Returns |ZX_ERR_BAD_STATE| if there is an existing sessionmgr process, and does not
  // start a new session.
  //
  // Returns fpromise::ok if a new session was started successfully.
  StartSessionResult StartSession(std::optional<ViewParams> view_params);

  // |AsyncHolder|
  // Asynchronously tears down the sessionmgr process. |callback| is invoked
  // once teardown is complete or has timed out.
  // Should be called through |AsyncHolder.Teardown|, not directly.
  void Teardown(fit::function<void()> callback);

  // Asynchronously tears down the sessionmgr process. |callback| is invoked
  // once teardown is complete or has timed out.
  void Shutdown(fit::function<void()> callback);

  void MarkClockAsStarted();

  // Callback function for session_provider to invoke when there is no active session.
  void OnSessionShutdown(SessionContextImpl::ShutDownReason shutdown_reason);

  // Returns true if sessionmgr is running.
  bool is_session_running() const { return !!session_context_; }

 private:
  // Check if the system should be rebooted per the session's policy. The
  // policy is as follows. The system should be rebooted if:
  //
  // * A shell agent has crashed 4 times within 1 hour
  // * AND no reboot has been recently.
  //
  // "recently" refers to an exponential backoff algorithm that rate limit
  // reboots. This is done to mitigate rabid boot loops when system restart
  // doesn't resolve the crashes.
  bool ShouldReboot(SessionContextImpl::ShutDownReason shutdown_reason);

  void TriggerReboot();

  fuchsia::sys::Launcher* const launcher_;                              // Not owned.
  fuchsia::hardware::power::statecontrol::Admin* const administrator_;  // Not owned.
  const modular::ModularConfigAccessor* const config_accessor_;         // Not owned.
  fit::function<void()> on_zero_sessions_;

  std::unique_ptr<SessionContextImpl> session_context_;

  // Service directory from which services will be served to child |sessionmgr|s.
  vfs::PseudoDir sessionmgr_service_dir_;

  // Names of services passed to sessionmgr.
  const std::vector<std::string> v2_services_for_sessionmgr_names_;
  // Directory of services passed to sessionmgr.
  const sys::ServiceDirectory v2_services_for_sessionmgr_dir_;

  // The basemgr outgoing directory (owned by basemgr) to which a directory of
  // V1 services may be exposed, from sessionmgr to basemgr and its children.
  vfs::PseudoDir* const outgoing_dir_root_;  // Not owned

  // The number of times that session had to be recovered from a crash, during a
  // given timeout. If the count exceed the max retry limit, a device
  // reboot will be triggered
  int session_crash_recovery_counter_ = 0;

  // The timestamp of when last crash happened
  zx::time last_crash_time_;

  // Helper object that enables this class to rate limit reboot attempts.
  RebootRateLimiter reboot_rate_limiter_;

  bool clock_started_ = false;

  FXL_DISALLOW_COPY_AND_ASSIGN(SessionProvider);
};

}  // namespace modular

#endif  // SRC_MODULAR_BIN_BASEMGR_SESSION_PROVIDER_H_
