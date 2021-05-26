// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MODULAR_LIB_SESSION_SESSION_H_
#define SRC_MODULAR_LIB_SESSION_SESSION_H_

#include <fuchsia/modular/internal/cpp/fidl.h>
#include <fuchsia/modular/session/cpp/fidl.h>
#include <lib/async/dispatcher.h>
#include <lib/fit/promise.h>
#include <lib/fit/result.h>

namespace modular::session {

enum class BasemgrRuntimeState {
  // basemgr is running as a v2 session.
  kV2Session,

  // basemgr is running as a v1 component.
  kV1Component,
};

// Returns the state of a running Modular instance, or std::nullopt if not running.
std::optional<BasemgrRuntimeState> GetBasemgrRuntimeState();

// Returns true if basemgr is running, either as a v1 component or a v2 session.
bool IsBasemgrRunning();

// Launches Modular with the given configuration.
//
// If there is a running session that exposes the |fuchsia.modular.session.Launcher| protocol,
// it is used to instruct the session to launch sessionmgr. The protocol must exist in a path
// under /hub-v2 in the process namespace. If the protocol is not available, launches basemgr
// as a v1 component.
//
// If basemgr is already running as a v1 component, it will be shut down first.
// If basemgr is already running as a session, it will *not* be shut down to ensure that
// it can be used to launch sessionmgr.
//
// |launcher| and |dispatcher| are only used to launch basemgr as a v1 component.
// |dispatcher| is used to serve the component's incoming directory.
// If |dispatcher| is null, the current thread must have a default async_dispatcher_t.
fit::promise<void, zx_status_t> Launch(fuchsia::sys::Launcher* launcher,
                                       fuchsia::modular::session::ModularConfig config,
                                       async_dispatcher_t* dispatcher = nullptr);

// Launches basemgr as a v1 component with the given configuration.
//
// |dispatcher| is used to serve the component's incoming directory.
// If |dispatcher| is null, the current thread must have a default async_dispatcher_t.
fit::promise<void, zx_status_t> LaunchBasemgrV1(fuchsia::sys::Launcher* launcher,
                                                fuchsia::modular::session::ModularConfig config,
                                                async_dispatcher_t* dispatcher = nullptr);

// Launches an instance of sessionmgr with the given configuration using the
// |fuchsia.modular.session.Launcher| protocol exposed by a session.
//
// The session's |Launcher| protocol is found in a path under /hub-v2
// in this component's namespace.
//
// # Errors
//
// ZX_ERR_NOT_FOUND: session is not running or it does not expose |Launcher|
fit::result<void, zx_status_t> LaunchSessionmgr(fuchsia::modular::session::ModularConfig config);

// Shuts down any currently running instance of basemgr.
fit::promise<void, zx_status_t> MaybeShutdownBasemgr();

// Clears the persisted Modular configuration by invoking basemgr as a v1 component
// with the "delete_persistent_config" argument.
fit::promise<void, zx_status_t> DeletePersistentConfig(fuchsia::sys::Launcher* launcher);

// Connects to the BasemgrDebug protocol served by the currently running instance of basemgr.
//
// # Errors
//
// ZX_ERR_NOT_FOUND: basemgr is not running or service connection failed
fit::result<fuchsia::modular::internal::BasemgrDebugPtr, zx_status_t> ConnectToBasemgrDebug();

}  // namespace modular::session

#endif  // SRC_MODULAR_LIB_SESSION_SESSION_H_
