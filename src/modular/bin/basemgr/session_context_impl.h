// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MODULAR_BIN_BASEMGR_SESSION_CONTEXT_IMPL_H_
#define SRC_MODULAR_BIN_BASEMGR_SESSION_CONTEXT_IMPL_H_

#include <fuchsia/auth/cpp/fidl.h>
#include <fuchsia/modular/auth/cpp/fidl.h>
#include <fuchsia/modular/cpp/fidl.h>
#include <fuchsia/modular/internal/cpp/fidl.h>
#include <fuchsia/sys/cpp/fidl.h>
#include <fuchsia/ui/policy/cpp/fidl.h>
#include <fuchsia/ui/views/cpp/fidl.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/fit/function.h>

#include "src/lib/fxl/macros.h"
#include "src/modular/lib/async/cpp/future.h"
#include "src/modular/lib/fidl/app_client.h"
#include "src/modular/lib/fidl/environment.h"

namespace modular {

// |SessionContextImpl| starts and manages a Sessionmgr. The life time of a
// Sessionmgr is bound to this class. |SessionContextImpl| is not self-owned,
// but still drives its own deletion: On logout, it signals its
// owner (BasemgrImpl) to delete it.
class SessionContextImpl : fuchsia::modular::internal::SessionContext {
 public:
  enum class ShutDownReason {
    // normal mode of shutdown
    LOGGED_OUT,
    // sessionmgr or session_shell crashed.
    CRASHED
  };

  // Called after performing shutdown of the session, to signal our completion
  // (and deletion of our instance) to our owner, this is done using a callback
  // supplied in the constructor. (The alternative is to take in a
  // SessionProvider*, which seems a little specific and overscoped).
  using OnSessionShutdownCallback =
      fit::function<void(ShutDownReason shutdown_reason, bool logout_users)>;

  // Called when sessionmgr requests to acquire the presentation.
  using GetPresentationCallback =
      fit::function<void(fidl::InterfaceRequest<fuchsia::ui::policy::Presentation> request)>;

  // |session_id| is the device-local unique identifier for this session. Caller
  // must ensure its uniqueness. sessionmgr creates an Environment namespace
  // with the given |session_id|, and this will crash if it tries to create an
  // environment with a pre-existing name.
  //
  // |additional_services| are services that will be installed into the
  // Sessionmgr's namespace, including an implementation of
  // `fuchsia.intl.PropertyProvider`.
  SessionContextImpl(fuchsia::sys::Launcher* const launcher, std::string session_id,
                     fuchsia::modular::AppConfig sessionmgr_config,
                     fuchsia::modular::AppConfig session_shell_config,
                     fuchsia::modular::AppConfig story_shell_config,
                     bool use_session_shell_for_story_shell_factory,
                     fidl::InterfaceHandle<fuchsia::auth::TokenManager> agent_token_manager,
                     fuchsia::modular::auth::AccountPtr account,
                     fuchsia::ui::views::ViewToken view_token,
                     fuchsia::sys::ServiceListPtr additional_services, zx::channel config_handle,
                     GetPresentationCallback get_presentation,
                     OnSessionShutdownCallback on_session_shutdown);

  ~SessionContextImpl() override = default;

  // This will effectively tear down the entire instance by calling
  // |on_session_shutdown_|. If |logout_users| is true, all the users will be
  // logged out with the assumption that all users belong to the current
  // session.
  void Shutdown(bool logout_users, fit::function<void()> callback);

  // Stops the active session shell, and starts the session shell specified in
  // |session_shell_config|.
  FuturePtr<> SwapSessionShell(fuchsia::modular::AppConfig session_shell_config);

 private:
  // Determines where current configurations are being read from, and forwards
  // that directory into a flat namespace that will be added to sessionmgr's
  // launch info.
  fuchsia::sys::FlatNamespacePtr MakeConfigNamespace(zx::channel config_handle);

  // |fuchsia::modular::internal::SessionContext|
  void Logout() override;

  // |fuchsia::modular::internal::SessionContext|
  void Restart() override;

  // |fuchsia::modular::internal::SessionContext|
  void Shutdown() override;

  // |fuchsia::modular::internal::SessionContext|
  void GetPresentation(fidl::InterfaceRequest<fuchsia::ui::policy::Presentation> request) override;

  std::unique_ptr<AppClient<fuchsia::modular::Lifecycle>> sessionmgr_app_;
  fuchsia::modular::internal::SessionmgrPtr sessionmgr_;

  fidl::Binding<fuchsia::modular::internal::SessionContext> session_context_binding_;

  std::vector<fit::function<void()>> shutdown_callbacks_;

  GetPresentationCallback get_presentation_;
  OnSessionShutdownCallback on_session_shutdown_;

  fxl::WeakPtrFactory<SessionContextImpl> weak_factory_;

  FXL_DISALLOW_COPY_AND_ASSIGN(SessionContextImpl);
};

}  // namespace modular

#endif  // SRC_MODULAR_BIN_BASEMGR_SESSION_CONTEXT_IMPL_H_
