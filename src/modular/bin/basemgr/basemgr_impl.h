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
#include <fuchsia/session/cpp/fidl.h>
#include <fuchsia/session/scene/cpp/fidl.h>
#include <fuchsia/sys/cpp/fidl.h>
#include <fuchsia/ui/policy/cpp/fidl.h>
#include <lib/async/cpp/executor.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/fit/function.h>
#include <lib/fpromise/promise.h>
#include <lib/inspect/cpp/inspect.h>
#include <lib/sys/cpp/outgoing_directory.h>

#include <memory>
#include <optional>
#include <variant>

#include "src/lib/fxl/macros.h"
#include "src/modular/bin/basemgr/child_listener.h"
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
                    public fuchsia::process::lifecycle::Lifecycle {
 public:
  using SceneOwnerPtr =
      std::variant<fuchsia::ui::policy::PresenterPtr, fuchsia::session::scene::ManagerPtr>;

  using LauncherBinding =
      fidl::Binding<fuchsia::modular::session::Launcher, std::unique_ptr<LauncherImpl>>;
  using LauncherBindingSet =
      fidl::BindingSet<fuchsia::modular::session::Launcher, std::unique_ptr<LauncherImpl>>;

  enum class State {
    // Normal mode of operation.
    RUNNING,
    // basemgr is restarting the session.
    RESTARTING,
    // basemgr is shutting down.
    SHUTTING_DOWN
  };

  // Creates a BasemgrImpl instance with the given parameters:
  //
  // |config_accessor| Contains configuration for starting sessions.
  //    This is normally read from files in basemgr's /config/data directory.
  // |outgoing| The component's outgoing directory for publishing protocols.
  // |launcher| Environment service for creating component instances.
  // |scene_owner| Legacy Service to initialize the presentation.
  // |graphical_presenter| Service to initialize the presentation.
  // |child_listener| Active connections to child components.
  // |view_provider| Connection to ViewProvider exposed by a v2 session shell.
  // |on_shutdown| Callback invoked when this basemgr instance is shutdown.
  BasemgrImpl(modular::ModularConfigAccessor config_accessor,
              std::shared_ptr<sys::OutgoingDirectory> outgoing_services, bool use_flatland,
              fuchsia::sys::LauncherPtr launcher, SceneOwnerPtr scene_owner,
              fuchsia::hardware::power::statecontrol::AdminPtr device_administrator,
              fuchsia::session::RestarterPtr session_restarter,
              std::unique_ptr<ChildListener> child_listener,
              std::optional<fuchsia::ui::app::ViewProviderPtr> view_provider,
              fit::function<void()> on_shutdown);

  ~BasemgrImpl() override;

  // Starts a session using the configuration read from |config_accessor_|.
  void Start();

  // |fuchsia::modular::Lifecycle|
  void Terminate() override;

  // |fuchsia::process::lifecycle::Lifecycle|
  void Stop() override;

  // Launches sessionmgr with the given |config|.
  void LaunchSessionmgr(fuchsia::modular::session::ModularConfig config);

  State state() const { return state_; }

  // Returns a function that connects the request for the |Launcher| protocol.
  //
  // The |Launcher| implementation delegates all calls back to this instance of |BasemgrImpl|.
  fidl::InterfaceRequestHandler<fuchsia::modular::session::Launcher> GetLauncherHandler();

 private:
  using StartSessionResult = fpromise::result<void, zx_status_t>;

  // Shuts down the session and session launcher component, if any are running.
  void Shutdown();

  void RestartSession(fit::closure on_restart_complete);

  // Starts a new session.
  //
  // Requires that |session_provider_| exists but is not running a session.
  //
  // Returns |ZX_ERR_BAD_STATE| if basemgr is shutting down, |session_provider_| does not exist,
  // or a session is already running.
  StartSessionResult StartSession();

  // Creates a |session_provider_| that uses the given config.
  //
  // |config_accessor| must live for the duration of the session, outliving |session_provider_|.
  void CreateSessionProvider(const ModularConfigAccessor* config_accessor);

  // Contains initial basemgr and sessionmgr configuration.
  modular::ModularConfigAccessor config_accessor_;

  // Contains configuration passed in via |Launcher.LaunchSessionmgr|
  std::unique_ptr<modular::ModularConfigAccessor> launch_sessionmgr_config_accessor_;

  // Used to export protocols like Lifecycle
  const std::shared_ptr<sys::OutgoingDirectory> outgoing_services_;

  // Used to launch component instances.
  fuchsia::sys::LauncherPtr launcher_;

  // Used to connect the session's view to the scene owner.
  SceneOwnerPtr scene_owner_;
  fuchsia::ui::policy::PresentationPtr presentation_;

  // Used to connect the session's view to scene_manager.
  fuchsia::session::scene::ManagerPtr scene_manager_;

  // Used to listen to child components and restart on crashes.
  std::unique_ptr<ChildListener> child_listener_;

  // Used to trigger device reboot.
  fuchsia::hardware::power::statecontrol::AdminPtr device_administrator_;

  // Used to trigger session restart.
  fuchsia::session::RestarterPtr session_restarter_;

  // Used to get the v2 session shell's view.
  std::optional<fuchsia::ui::app::ViewProviderPtr> view_provider_;

  fit::function<void()> on_shutdown_;

  LauncherBindingSet session_launcher_bindings_;
  fidl::BindingSet<fuchsia::modular::Lifecycle> lifecycle_bindings_;
  fidl::BindingSet<fuchsia::process::lifecycle::Lifecycle> process_lifecycle_bindings_;

  AsyncHolder<SessionProvider> session_provider_;

  async::Executor executor_;

  State state_ = State::RUNNING;

  const bool use_flatland_;

  fxl::WeakPtrFactory<BasemgrImpl> weak_factory_;

  FXL_DISALLOW_COPY_AND_ASSIGN(BasemgrImpl);
};

}  // namespace modular

#endif  // SRC_MODULAR_BIN_BASEMGR_BASEMGR_IMPL_H_
