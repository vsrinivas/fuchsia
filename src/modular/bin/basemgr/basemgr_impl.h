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
#include <lib/fidl/cpp/binding.h>
#include <lib/fit/function.h>
#include <lib/svc/cpp/service_namespace.h>

#include "src/lib/fxl/macros.h"
#include "src/modular/bin/basemgr/cobalt/cobalt.h"
#include "src/modular/bin/basemgr/presentation_container.h"
#include "src/modular/bin/basemgr/session_provider.h"
#include "src/modular/lib/async/cpp/future.h"

namespace modular {

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
  // Initializes as BasemgrImpl instance with the given parameters:
  //
  // |config| Configs that are parsed from command line. These will be read from
  // a configuration file with the completion of MF-10. Used to configure
  // the modular framework environment.
  // |launcher| Environment service for creating component instances.
  // |presenter| Service to initialize the presentation.
  // |on_shutdown| Callback invoked when this basemgr instance is shutdown.
  explicit BasemgrImpl(fuchsia::modular::session::ModularConfig config,
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

 private:
  FuturePtr<> StopScenic();

  // Starts the basemgr functionalities in the following order:
  // 1. Initialize session provider.
  // 2. Launch a session.
  void Start();

  void Shutdown() override;

  // Starts a new session.
  void StartSession(bool use_random_id);

  // Updates the session shell app config to the active session shell. Done once
  // on initialization and every time the session shells are swapped.
  void UpdateSessionShellConfig();

  // |BasemgrDebug|
  void RestartSession(RestartSessionCallback on_restart_complete) override;

  // |BasemgrDebug|
  void StartSessionWithRandomId() override;

  // |SessionProvider::Delegate|
  void GetPresentation(fidl::InterfaceRequest<fuchsia::ui::policy::Presentation> request) override;

  fuchsia::modular::session::ModularConfig config_;

  // Used to configure which session shell component to launch.
  fuchsia::modular::session::AppConfig session_shell_config_;

  // Retained to be used in creating a `SessionProvider`.
  const std::shared_ptr<sys::ServiceDirectory> component_context_services_;

  // Used to export protocols like IntlPropertyProviderImpl and Lifecycle
  const std::shared_ptr<sys::OutgoingDirectory> outgoing_services_;

  // Used to launch component instances, such as the base shell.
  fuchsia::sys::LauncherPtr launcher_;
  // Used to connect the |presentation_container_| to scenic.
  fuchsia::ui::policy::PresenterPtr presenter_;
  // Used to trigger device reboot.
  fuchsia::hardware::power::statecontrol::AdminPtr device_administrator_;
  fit::function<void()> on_shutdown_;

  // Holds the presentation service.
  std::unique_ptr<PresentationContainer> presentation_container_;

  fidl::BindingSet<fuchsia::modular::Lifecycle> lifecycle_bindings_;
  fidl::BindingSet<fuchsia::modular::internal::BasemgrDebug> basemgr_debug_bindings_;
  fidl::BindingSet<fuchsia::process::lifecycle::Lifecycle> process_lifecycle_bindings_;

  fuchsia::ui::lifecycle::LifecycleControllerPtr scenic_lifecycle_controller_;

  bool use_random_session_id_{true};

  AsyncHolder<SessionProvider> session_provider_;

  enum class State {
    // normal mode of operation
    RUNNING,
    // basemgr is shutting down.
    SHUTTING_DOWN
  };

  State state_ = State::RUNNING;

  FXL_DISALLOW_COPY_AND_ASSIGN(BasemgrImpl);
};

}  // namespace modular

#endif  // SRC_MODULAR_BIN_BASEMGR_BASEMGR_IMPL_H_
