// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MODULAR_BIN_BASEMGR_SESSION_PROVIDER_H_
#define SRC_MODULAR_BIN_BASEMGR_SESSION_PROVIDER_H_

#include <fuchsia/hardware/power/statecontrol/cpp/fidl.h>
#include <fuchsia/modular/cpp/fidl.h>
#include <fuchsia/modular/session/cpp/fidl.h>
#include <fuchsia/ui/policy/cpp/fidl.h>
#include <fuchsia/ui/views/cpp/fidl.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/sys/cpp/service_directory.h>
#include <lib/ui/scenic/cpp/view_ref_pair.h>
#include <lib/vfs/cpp/pseudo_dir.h>

#include "src/modular/bin/basemgr/session_context_impl.h"
#include "src/modular/lib/async/cpp/future.h"
#include "src/modular/lib/modular_config/modular_config_accessor.h"

namespace modular {

class SessionProvider {
 public:
  // Users of SessionProvider must register a Delegate object, which provides
  // functionality to SessionProvider that's outside the scope of this class.
  class Delegate {
   public:
    // Called when a session provided by SessionProvider wants to acquire
    // presentation.
    virtual void GetPresentation(
        fidl::InterfaceRequest<fuchsia::ui::policy::Presentation> request) = 0;
  };

  using StartSessionResult = fpromise::result<void, zx_status_t>;

  // Target constructor.
  //
  // |on_zero_sessions| is invoked when all sessions have been deleted. This is
  // meant to be a callback for BasemgrImpl to start a new session.
  SessionProvider(Delegate* delegate, fuchsia::sys::Launcher* launcher,
                  fuchsia::hardware::power::statecontrol::Admin* administrator,
                  const modular::ModularConfigAccessor* config_accessor,
                  fuchsia::sys::ServiceList v2_services_for_sessionmgr,
                  vfs::PseudoDir* outgoing_dir_root, fit::function<void()> on_zero_sessions);

  // Starts a new sessionmgr process if there isn't one already.
  //
  // Returns |ZX_ERR_BAD_STATE| if there is an existing sessionmgr process, and does not
  // start a new session.
  //
  // Returns fpromise::ok if a new session was started successfully.
  StartSessionResult StartSession(fuchsia::ui::views::ViewToken view_token,
                                  scenic::ViewRefPair view_ref_pair);

  // Asynchronously tears down the sessionmgr process. |callback| is invoked
  // once teardown is complete or has timed out.
  void Teardown(fit::function<void()> callback);

  // Callback function for session_provider to invoke when there is no active session.
  void OnSessionShutdown(SessionContextImpl::ShutDownReason shutdown_reason);

  // Shuts down the running session, causing a new session to be created.
  void RestartSession(fit::function<void()> on_restart_complete);

  // Returns true if sessionmgr is running.
  bool is_session_running() const { return !!session_context_; }

 private:
  Delegate* const delegate_;                                            // Neither owned nor copied.
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

  FXL_DISALLOW_COPY_AND_ASSIGN(SessionProvider);
};

}  // namespace modular

#endif  // SRC_MODULAR_BIN_BASEMGR_SESSION_PROVIDER_H_
