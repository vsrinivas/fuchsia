// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MODULAR_BIN_BASEMGR_SESSION_CONTEXT_IMPL_H_
#define SRC_MODULAR_BIN_BASEMGR_SESSION_CONTEXT_IMPL_H_

#include <fuchsia/modular/cpp/fidl.h>
#include <fuchsia/modular/internal/cpp/fidl.h>
#include <fuchsia/modular/session/cpp/fidl.h>
#include <fuchsia/sys/cpp/fidl.h>
#include <fuchsia/ui/policy/cpp/fidl.h>
#include <fuchsia/ui/views/cpp/fidl.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/fit/function.h>

#include <optional>

#include "src/lib/fxl/macros.h"
#include "src/modular/lib/async/cpp/future.h"
#include "src/modular/lib/fidl/app_client.h"
#include "src/modular/lib/fidl/environment.h"
#include "src/modular/lib/modular_config/modular_config_accessor.h"

namespace modular {

// |SessionContextImpl| starts and manages a Sessionmgr. The life time of a
// Sessionmgr is bound to this class. |SessionContextImpl| is not self-owned,
// but still drives its own deletion: On logout, it signals its
// owner (BasemgrImpl) to delete it.
class SessionContextImpl : fuchsia::modular::internal::SessionContext {
 public:
  enum class ShutDownReason {
    // normal mode of shutdown
    CLIENT_REQUEST,
    // sessionmgr or session_shell crashed.
    CRITICAL_FAILURE
  };

  // Called after performing shutdown of the session, to signal our completion
  // (and deletion of our instance) to our owner, this is done using a callback
  // supplied in the constructor. (The alternative is to take in a
  // SessionProvider*, which seems a little specific and overscoped).
  using OnSessionShutdownCallback = fit::function<void(ShutDownReason shutdown_reason)>;

  // Called when sessionmgr requests to acquire the presentation.
  using GetPresentationCallback =
      fit::function<void(fidl::InterfaceRequest<fuchsia::ui::policy::Presentation> request)>;

  // |additional_services| are services that will be installed into the
  // Sessionmgr's namespace, including an implementation of
  // `fuchsia.intl.PropertyProvider`.
  SessionContextImpl(fuchsia::sys::Launcher* launcher,
                     fuchsia::modular::session::AppConfig sessionmgr_app_config,
                     const modular::ModularConfigAccessor* config_accessor,
                     fuchsia::ui::views::ViewToken view_token,
                     fuchsia::sys::ServiceListPtr additional_services_for_sessionmgr,
                     fuchsia::sys::ServiceList additional_services_for_agents,
                     GetPresentationCallback get_presentation,
                     OnSessionShutdownCallback on_session_shutdown);

  ~SessionContextImpl() override = default;

  // Calls |on_session_shutdown_|, which normally also results in a new
  // session being started. Calls |callback| when complete.
  void Shutdown(ShutDownReason reason, fit::function<void()> callback);

 private:
  // Creates and serves a PseudoDir that contains a config file with the given contents.
  //
  // Returns a FlatNamespace that contains the file inside the overridden Modular config directory.
  // When sessionmgr is started with this namespace, it can can access the config file at
  // /config_override/data/startup.config
  fuchsia::sys::FlatNamespacePtr CreateAndServeConfigNamespace(std::string config_contents);

  // |fuchsia::modular::internal::SessionContext|
  void Restart() override;

  // |fuchsia::modular::internal::SessionContext|
  void RestartDueToCriticalFailure() override;

  // |fuchsia::modular::internal::SessionContext|
  void GetPresentation(fidl::InterfaceRequest<fuchsia::ui::policy::Presentation> request) override;

  std::unique_ptr<AppClient<fuchsia::modular::Lifecycle>> sessionmgr_app_;
  fuchsia::modular::internal::SessionmgrPtr sessionmgr_;

  fidl::Binding<fuchsia::modular::internal::SessionContext> session_context_binding_;

  GetPresentationCallback get_presentation_;
  OnSessionShutdownCallback on_session_shutdown_;

  std::vector<fit::function<void()>> shutdown_callbacks_;

  // A directory used to serve the configuration to sessionmgr.
  std::unique_ptr<vfs::PseudoDir> config_dir_;

  fxl::WeakPtrFactory<SessionContextImpl> weak_factory_;

  FXL_DISALLOW_COPY_AND_ASSIGN(SessionContextImpl);
};

}  // namespace modular

#endif  // SRC_MODULAR_BIN_BASEMGR_SESSION_CONTEXT_IMPL_H_
