// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/modular/lib/session/session.h"

#include <lib/fdio/directory.h>
#include <lib/fit/bridge.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/vfs/cpp/pseudo_dir.h>
#include <lib/vfs/cpp/pseudo_file.h>

#include "src/lib/files/glob.h"
#include "src/lib/files/path.h"
#include "src/modular/lib/modular_config/modular_config.h"
#include "src/modular/lib/modular_config/modular_config_constants.h"
#include "src/modular/lib/session/session_constants.h"

namespace modular::session {

namespace {

// Connects to a protocol served at the first path that matches one of the given glob patterns.
//
// # Errors
//
// ZX_ERR_NOT_FOUND: No path exists that matches a pattern in |glob_paths|, or if connecting
// to a matching path was unsuccessful.
template <typename Interface, typename InterfacePtr = fidl::InterfacePtr<Interface>>
fit::result<InterfacePtr, zx_status_t> ConnectInPaths(
    std::initializer_list<std::string> glob_paths) {
  files::Glob glob(glob_paths);

  for (const std::string& path : glob) {
    InterfacePtr ptr;
    if (fdio_service_connect(path.c_str(), ptr.NewRequest().TakeChannel().get()) == ZX_OK) {
      return fit::ok(std::move(ptr));
    }
  }

  return fit::error(ZX_ERR_NOT_FOUND);
}

// Creates a |PseudoDir| that contains a configuration file with the contents |config_str|.
std::unique_ptr<vfs::PseudoDir> CreateConfigPseudoDir(std::string config_str) {
  auto dir = std::make_unique<vfs::PseudoDir>();
  dir->AddEntry(modular_config::kStartupConfigFilePath,
                std::make_unique<vfs::PseudoFile>(
                    config_str.length(), [config_str = std::move(config_str)](
                                             std::vector<uint8_t>* out, size_t /*unused*/) {
                      std::copy(config_str.begin(), config_str.end(), std::back_inserter(*out));
                      return ZX_OK;
                    }));
  return dir;
}

}  // namespace

bool IsRunning() {
  // If there exists a path that exposes the BasemgrDebug protocol, then basemgr is running.
  return files::Glob({kBasemgrDebugSessionGlob, kBasemgrDebugV1Glob}).size() != 0;
}

fit::promise<void, zx_status_t> Launch(fuchsia::sys::Launcher* launcher,
                                       fuchsia::modular::session::ModularConfig config,
                                       async_dispatcher_t* dispatcher) {
  auto shutdown_if_running = fit::make_promise([]() -> fit::promise<void, zx_status_t> {
    if (IsRunning()) {
      return Shutdown();
    }
    return fit::make_result_promise<void, zx_status_t>(fit::ok());
  });

  return shutdown_if_running
      .or_else([](const zx_status_t& status) {
        FX_PLOGS(ERROR, status) << "Could not shut down running instance of basemgr";
        return fit::error(status);
      })
      .and_then([launcher, config = std::move(config), dispatcher]() {
        fit::bridge<void, zx_status_t> bridge;

        // Create the pseudo directory with our config "file" mapped to kConfigFilename.
        auto config_dir = CreateConfigPseudoDir(modular::ConfigToJsonString(config));
        fidl::InterfaceHandle<fuchsia::io::Directory> dir_handle;
        config_dir->Serve(fuchsia::io::OPEN_RIGHT_READABLE, dir_handle.NewRequest().TakeChannel(),
                          dispatcher);

        // Build a LaunchInfo with the config directory above mapped to /config_override/data.
        fuchsia::sys::LaunchInfo launch_info;
        launch_info.url = kBasemgrV1Url;
        launch_info.flat_namespace = fuchsia::sys::FlatNamespace::New();
        launch_info.flat_namespace->paths.push_back(modular_config::kOverriddenConfigDir);
        launch_info.flat_namespace->directories.push_back(dir_handle.TakeChannel());

        // Complete when basemgr's out directory has been mounted.
        fuchsia::sys::ComponentControllerPtr controller;
        controller.events().OnDirectoryReady = [completer = std::move(bridge.completer)]() mutable {
          completer.complete_ok();
        };

        // Launch a basemgr instance with the custom namespace we created above.
        launcher->CreateComponent(std::move(launch_info), controller.NewRequest());

        return bridge.consumer.promise().and_then(
            [controller = std::move(controller), config_dir = std::move(config_dir)]() {
              controller->Detach();
            });
      });
}

fit::promise<void, zx_status_t> Shutdown() {
  if (!IsRunning()) {
    return fit::make_result_promise<void, zx_status_t>(fit::ok());
  }

  // Get a connection to BasemgrDebug in order to shut basemgr down.
  auto basemgr_debug_result = ConnectToBasemgrDebug();
  if (basemgr_debug_result.is_error()) {
    FX_PLOGS(ERROR, basemgr_debug_result.error()) << "Could not connect to BasemgrDebug protocol";
    return fit::make_error_promise(basemgr_debug_result.take_error());
  }
  auto basemgr_debug = basemgr_debug_result.take_value();

  basemgr_debug->Shutdown();

  fit::bridge<void, zx_status_t> bridge;

  // Wait for basemgr to shutdown.
  basemgr_debug.set_error_handler(
      [completer = std::move(bridge.completer)](zx_status_t status) mutable {
        if (status == ZX_OK) {
          completer.complete_ok();
        } else {
          completer.complete_error(status);
        }
      });

  return bridge.consumer.promise().and_then(
      [basemgr_debug =
           std::move(basemgr_debug)]() { /* Keep |basemgr_debug| alive until completed */ });
}

fit::promise<void, zx_status_t> DeletePersistentConfig(fuchsia::sys::Launcher* launcher) {
  fuchsia::sys::LaunchInfo launch_info;
  launch_info.url = kBasemgrV1Url;
  launch_info.arguments = {"delete_persistent_config"};

  fuchsia::sys::ComponentControllerPtr controller;
  launcher->CreateComponent(std::move(launch_info), controller.NewRequest());

  fit::bridge<void, zx_status_t> on_terminated_bridge;
  fit::bridge<void, zx_status_t> error_handler_bridge;

  controller.events().OnTerminated = [completer = std::move(on_terminated_bridge.completer)](
                                         int64_t exit_code,
                                         fuchsia::sys::TerminationReason reason) mutable {
    if (reason != fuchsia::sys::TerminationReason::EXITED || exit_code != EXIT_SUCCESS) {
      FX_LOGS(ERROR) << "`basemgr delete_peristent_config` did not exit cleanly: reason = "
                     << static_cast<int>(reason) << ", exit code = " << exit_code;
      // The termination reason and exit code do not map directly to zx_status_t.
      completer.complete_error(ZX_ERR_INTERNAL);
    } else {
      completer.complete_ok();
    }
  };

  controller.set_error_handler(
      [completer = std::move(error_handler_bridge.completer)](zx_status_t status) mutable {
        if (status == ZX_OK) {
          completer.complete_ok();
        } else {
          completer.complete_error(status);
        }
      });

  return fit::join_promises(on_terminated_bridge.consumer.promise(),
                            error_handler_bridge.consumer.promise())
      .then([controller = std::move(controller)](
                fit::result<
                    std::tuple<fit::result<void, zx_status_t>, fit::result<void, zx_status_t>>>&
                    result) -> fit::result<void, zx_status_t> {
        if (result.is_error()) {
          // Running the joined promises failed.
          return fit::error(ZX_ERR_INTERNAL);
        }

        auto on_terminated_result = std::get<0>(result.value());
        if (on_terminated_result.is_error()) {
          return on_terminated_result;
        }

        auto error_handler_result = std::get<1>(result.value());
        if (error_handler_result.is_error() && error_handler_result.error() != ZX_ERR_PEER_CLOSED) {
          return error_handler_result;
        }

        return fit::ok();
      });
}

fit::result<fuchsia::modular::internal::BasemgrDebugPtr, zx_status_t> ConnectToBasemgrDebug() {
  return ConnectInPaths<fuchsia::modular::internal::BasemgrDebug>(
      {kBasemgrDebugSessionGlob, kBasemgrDebugV1Glob});
}

}  // namespace modular::session
