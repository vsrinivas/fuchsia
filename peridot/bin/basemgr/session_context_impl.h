// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_BASEMGR_SESSION_CONTEXT_IMPL_H_
#define PERIDOT_BIN_BASEMGR_SESSION_CONTEXT_IMPL_H_

#include <fuchsia/auth/cpp/fidl.h>
#include <fuchsia/modular/auth/cpp/fidl.h>
#include <fuchsia/modular/cpp/fidl.h>
#include <fuchsia/modular/internal/cpp/fidl.h>
#include <fuchsia/sys/cpp/fidl.h>
#include <fuchsia/ui/policy/cpp/fidl.h>
#include <fuchsia/ui/viewsv1token/cpp/fidl.h>
#include <lib/async/cpp/future.h>
#include <lib/component/cpp/startup_context.h>
#include <lib/fidl/cpp/array.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/fidl/cpp/interface_handle.h>
#include <lib/fidl/cpp/interface_ptr_set.h>
#include <lib/fidl/cpp/interface_request.h>
#include <lib/fit/function.h>
#include <lib/fxl/macros.h>

#include "peridot/lib/fidl/app_client.h"
#include "peridot/lib/fidl/environment.h"

namespace modular {

// |SessionContextImpl| starts and manages a Sessionmgr. The life time of a
// Sessionmgr is bound to this class. |SessionContextImpl| is not self-owned,
// but still drives its own deletion: On logout, it signals its
// owner (BasemgrImpl) to delete it.
class SessionContextImpl : fuchsia::modular::internal::SessionContext {
 public:
  // After perfoming logout, to signal our completion (and deletion of our
  // instance) to our owner, we do it using a callback supplied to us in our
  // constructor. (The alternative is to take in a BasemgrImpl*, which seems
  // a little specific and overscoped).
  using DoneCallback = std::function<void(SessionContextImpl*)>;

  SessionContextImpl(
      fuchsia::sys::Launcher* const launcher,
      fuchsia::modular::AppConfig sessionmgr,
      fuchsia::modular::AppConfig session_shell,
      fuchsia::modular::AppConfig story_shell,
      fidl::InterfaceHandle<fuchsia::auth::TokenManager> ledger_token_manager,
      fidl::InterfaceHandle<fuchsia::auth::TokenManager> agent_token_manager,
      fuchsia::modular::auth::AccountPtr account,
      fidl::InterfaceRequest<fuchsia::ui::viewsv1token::ViewOwner>
          view_owner_request,
      fidl::InterfaceHandle<fuchsia::sys::ServiceProvider> base_shell_services,
      DoneCallback done);

  // This will effectively tear down the entire instance by calling |done_|.
  void Logout(fit::function<void()> callback);

  // Stops the active session shell, and starts the session shell specified in
  // |session_shell_config|.
  FuturePtr<> SwapSessionShell(
      fuchsia::modular::AppConfig session_shell_config);

 private:
  // |fuchsia::modular::internal::SessionContext|
  void Logout() override;

  // |fuchsia::modular::internal::SessionContext|
  void GetPresentation(fidl::InterfaceRequest<fuchsia::ui::policy::Presentation>
                           request) override;

  std::unique_ptr<Environment> sessionmgr_environment_;
  std::unique_ptr<AppClient<fuchsia::modular::Lifecycle>> sessionmgr_app_;
  fuchsia::modular::internal::SessionmgrPtr sessionmgr_;

  fidl::Binding<fuchsia::modular::internal::SessionContext> session_context_binding_;

  std::vector<fit::function<void()>> logout_response_callbacks_;

  fuchsia::sys::ServiceProviderPtr base_shell_services_;

  DoneCallback done_;

  FXL_DISALLOW_COPY_AND_ASSIGN(SessionContextImpl);
};

}  // namespace modular

#endif  // PERIDOT_BIN_BASEMGR_SESSION_CONTEXT_IMPL_H_
