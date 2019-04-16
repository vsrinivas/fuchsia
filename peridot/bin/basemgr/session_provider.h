// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_BASEMGR_SESSION_PROVIDER_H_
#define PERIDOT_BIN_BASEMGR_SESSION_PROVIDER_H_

#include <fuchsia/auth/cpp/fidl.h>
#include <fuchsia/modular/auth/cpp/fidl.h>
#include <fuchsia/modular/cpp/fidl.h>
#include <fuchsia/ui/policy/cpp/fidl.h>
#include <fuchsia/ui/views/cpp/fidl.h>
#include <lib/async/cpp/future.h>

#include "peridot/bin/basemgr/session_context_impl.h"
#include "peridot/bin/basemgr/session_provider.h"
#include "peridot/bin/basemgr/user_provider_impl.h"

namespace modular {

class SessionProvider {
 public:
  // Users of SessionProvider must register a Delegate object, which provides
  // functionality to SessionProvider that's outside the scope of this class.
  class Delegate {
   public:
    // Called when SessionProvider wants to logout all users.
    virtual void LogoutUsers(fit::function<void()> callback) = 0;

    // Called when a session provided by SessionProvider wants to acquire
    // presentation.
    virtual void GetPresentation(
        fidl::InterfaceRequest<fuchsia::ui::policy::Presentation> request) = 0;
  };

  // |on_zero_sessions| is invoked when all sessions have been deleted. This is
  // meant to be a callback for BasemgrImpl to either display a base shell or
  // start a new session.
  SessionProvider(Delegate* const delegate,
                  fuchsia::sys::Launcher* const launcher,
                  fuchsia::modular::AppConfig sessionmgr,
                  fuchsia::modular::AppConfig session_shell,
                  fuchsia::modular::AppConfig story_shell,
                  bool use_session_shell_for_story_shell_factory,
                  fit::function<void()> on_zero_sessions);

  // Starts a new sessionmgr process if there isn't one already. Returns false
  // if there is an existing sessionmgr process, and does not start a new
  // session. Returns true if a new session was started successfully.
  bool StartSession(fuchsia::ui::views::ViewToken view_token,
                    fuchsia::modular::auth::AccountPtr account,
                    fuchsia::auth::TokenManagerPtr ledger_token_manager,
                    fuchsia::auth::TokenManagerPtr agent_token_manager);

  // Asynchronously tears down the sessionmgr process. |callback| is invoked
  // once teardown is complete or has timed out.
  void Teardown(fit::function<void()> callback);

  // Stops the active session shell, and starts the session shell specified in
  // |session_shell_config|. If no session shells are running, this has no
  // effect, and will return an immediately-completed future.
  FuturePtr<> SwapSessionShell(
      fuchsia::modular::AppConfig session_shell_config);

  // Shuts down the running session without logging any users out, which will
  // effectively restart a new session with the same users.
  void RestartSession(fit::function<void()> on_restart_complete);

 private:
  Delegate* const delegate_;                // Neither owned nor copied.
  fuchsia::sys::Launcher* const launcher_;  // Not owned.
  const fuchsia::modular::AppConfig sessionmgr_;
  const fuchsia::modular::AppConfig session_shell_;
  const fuchsia::modular::AppConfig story_shell_;
  bool use_session_shell_for_story_shell_factory_;

  fit::function<void()> on_zero_sessions_;
  std::unique_ptr<SessionContextImpl> session_context_;

  FXL_DISALLOW_COPY_AND_ASSIGN(SessionProvider);
};

}  // namespace modular

#endif  // PERIDOT_BIN_BASEMGR_SESSION_PROVIDER_H_
