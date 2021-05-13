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

// Returns true if basemgr is running, either as a v1 component or a v2 session.
bool IsRunning();

// Launches basemgr as a v1 component with the given configuration.
//
// If basemgr is already running, it will be shut down first.
//
// |dispatcher| is used to serve the component's incoming directory.
// If |dispatcher| is null, the current thread must have a default async_dispatcher_t.
fit::promise<void, zx_status_t> Launch(fuchsia::sys::Launcher* launcher,
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
fit::promise<void, zx_status_t> Shutdown();

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
