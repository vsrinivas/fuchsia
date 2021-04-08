// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MODULAR_BIN_BASEMGR_BASEMGR_IMPL_H_
#define SRC_MODULAR_BIN_BASEMGR_BASEMGR_IMPL_H_

#include <fuchsia/hardware/power/statecontrol/cpp/fidl.h>
#include <fuchsia/modular/cpp/fidl.h>
#include <fuchsia/modular/internal/cpp/fidl.h>
#include <fuchsia/modular/session/cpp/fidl.h>
#include <fuchsia/process/lifecycle/cpp/fidl.h>
#include <fuchsia/sys/cpp/fidl.h>
#include <fuchsia/ui/lifecycle/cpp/fidl.h>
#include <fuchsia/ui/policy/cpp/fidl.h>
#include <lib/async/cpp/executor.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/fit/function.h>
#include <lib/fit/promise.h>
#include <lib/svc/cpp/service_namespace.h>

#include <optional>

#include "src/lib/fxl/macros.h"
#include "src/modular/bin/basemgr/cobalt/cobalt.h"
#include "src/modular/bin/basemgr/presentation_container.h"
#include "src/modular/bin/basemgr/session_provider.h"
#include "src/modular/lib/async/cpp/future.h"
#include "src/modular/lib/modular_config/modular_config_accessor.h"

namespace modular {

class LauncherImpl;

// Basemgr is the parent process of the modular framework, and it is started by
// the sysmgr as part of the boot sequence.
//
// It has several high-level responsibilities:
// 1) Initializes and owns the system's root view and presentation.
// 2) Manages the lifecycle of sessions, represented as |sessionmgr| processes.
class BasemgrImpl : public fuchsia::modular::Lifecycle,
                    public fuchsia::process::lifecycle::Lifecycle,
                    fuchsia::modular::internal::BasemgrDebug,
                    modular::SessionProvider::Delegate {
 public:
  using LauncherBinding =
      fidl::Binding<fuchsia::modular::session::Launcher, std::unique_ptr<LauncherImpl>>;

  using LauncherBindingSet =
      fidl::BindingSet<fuchsia::modular::session::Launcher, std::unique_ptr<LauncherImpl>>;

  enum class State {
    // normal mode of operation
    RUNNING,
    // basemgr is shutting down.
    SHUTTING_DOWN
  };

  // Creates a BasemgrImpl instance with the given parameters:
  //
  // |config_accessor| Contains configuration for starting sessions.
  //    This is normally read from files in basemgr's /config/data directory.
  // |launcher| Environment service for creating component instances.
  // |presenter| Service to initialize the presentation.
  // |on_shutdown| Callback invoked when this basemgr instance is shutdown.
  explicit BasemgrImpl(modular::ModularConfigAccessor config_accessor,
                       std::shared_ptr<sys::ServiceDirectory> incoming_services,
                       std::shared_ptr<sys::OutgoingDirectory> outgoing_services,
                       fuchsia::sys::LauncherPtr launcher,
                       fuchsia::ui::policy::PresenterPtr presenter,
                       fuchsia::hardware::power::statecontrol::AdminPtr device_administrator,
                       fit::function<void()> on_shutdown);

  ~BasemgrImpl() override;

  void Connect(fidl::InterfaceRequest<fuchsia::modular::internal::BasemgrDebug> request);

  // |fuchsia::modular::Lifecycle|
  void Terminate() override;

  // |fuchsia::process::lifecycle::Lifecycle|
  void Stop() override;

  // Launches sessionmgr with the given |config|.
  void LaunchSessionmgr(fuchsia::modular::session::ModularConfig config,
                        fuchsia::sys::ServiceList services_from_session_launcher);

  State state() const { return state_; }

 private:
  using StartSessionResult = fit::result<void, zx_status_t>;

  // Stops the Scenic component.
  //
  // Resolves to an zx_status_t error if Scenic's lifecycle control channel closes for a reason
  // other than ZX_ERR_PEER_CLOSED.
  fit::promise<void, zx_status_t> StopScenic();

  // Starts the session launcher component, if configured, or starts a session using the
  // configuration read from |config_accessor_|.
  void Start();

  // Shuts down the session, session launcher component, and Scenic, if any are running.
  void Shutdown() override;

  // Starts a new session.
  //
  // Requires that |session_provider_| exists but is not running a session.
  //
  // Returns |ZX_ERR_BAD_STATE| if basemgr is shutting down, |session_provider_| does not exist,
  // or a session is already running.
  StartSessionResult StartSession();

  // |BasemgrDebug|
  void RestartSession(RestartSessionCallback on_restart_complete) override;

  // |BasemgrDebug|
  void StartSessionWithRandomId() override;

  // |SessionProvider::Delegate|
  void GetPresentation(fidl::InterfaceRequest<fuchsia::ui::policy::Presentation> request) override;

  // Starts the session launcher component defined in the default basemgr configuration.
  void StartSessionLauncherComponent();

  // Creates a |session_provider_| that uses the given config. If
  // |services_from_session_launcher| is populated all listed services will be
  // provided to agent children of sessionmgr.
  //
  // |config_accessor| must live for the duration of the session, outliving |session_provider_|.
  void CreateSessionProvider(const ModularConfigAccessor* config_accessor,
                             fuchsia::sys::ServiceList services_from_session_launcher);

  // Returns a service list for serving |fuchsia.modular.session.Launcher| to the session
  // launcher component, served from |session_launcher_component_service_dir_|.
  fuchsia::sys::ServiceListPtr CreateAndServeSessionLauncherComponentServices();

  // Contains initial basemgr and sessionmgr configuration.
  modular::ModularConfigAccessor config_accessor_;

  // Contains configuration passed in via |Launcher.LaunchSessionmgr|
  std::unique_ptr<modular::ModularConfigAccessor> launch_sessionmgr_config_accessor_;

  // Retained to be used in creating a `SessionProvider`.
  const std::shared_ptr<sys::ServiceDirectory> component_context_services_;

  // Used to export protocols like Lifecycle
  const std::shared_ptr<sys::OutgoingDirectory> outgoing_services_;

  // Used to launch component instances.
  fuchsia::sys::LauncherPtr launcher_;
  // Used to connect the |presentation_container_| to scenic.
  fuchsia::ui::policy::PresenterPtr presenter_;
  // Used to trigger device reboot.
  fuchsia::hardware::power::statecontrol::AdminPtr device_administrator_;
  fit::function<void()> on_shutdown_;

  // Holds the presentation service.
  std::unique_ptr<PresentationContainer> presentation_container_;

  LauncherBindingSet session_launcher_bindings_;
  fidl::BindingSet<fuchsia::modular::Lifecycle> lifecycle_bindings_;
  fidl::BindingSet<fuchsia::modular::internal::BasemgrDebug> basemgr_debug_bindings_;
  fidl::BindingSet<fuchsia::process::lifecycle::Lifecycle> process_lifecycle_bindings_;

  fuchsia::ui::lifecycle::LifecycleControllerPtr scenic_lifecycle_controller_;

  AsyncHolder<SessionProvider> session_provider_;

  std::unique_ptr<AppClient<fuchsia::modular::Lifecycle>> session_launcher_component_app_;

  // Service directory from which |fuchsia.modular.session.Launcher| is served to
  // session launcher components.
  vfs::PseudoDir session_launcher_component_service_dir_;

  async::Executor executor_;

  State state_ = State::RUNNING;

  FXL_DISALLOW_COPY_AND_ASSIGN(BasemgrImpl);
};

}  // namespace modular

#endif  // SRC_MODULAR_BIN_BASEMGR_BASEMGR_IMPL_H_
