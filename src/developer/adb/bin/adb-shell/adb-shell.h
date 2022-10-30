// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_ADB_BIN_ADB_SHELL_ADB_SHELL_H_
#define SRC_DEVELOPER_ADB_BIN_ADB_SHELL_ADB_SHELL_H_

#include <fidl/fuchsia.dash/cpp/wire.h>
#include <fidl/fuchsia.hardware.adb/cpp/wire.h>

#include <optional>
#include <string>

#include <fbl/auto_lock.h>

#include "src/developer/adb/bin/adb-shell/adb_shell_config.h"

namespace adb_shell {

class AdbShellImpl;

// Provides shell service to adb daemon.
class AdbShell : public fidl::WireServer<fuchsia_hardware_adb::Provider> {
 public:
  explicit AdbShell(fidl::ClientEnd<fuchsia_io::Directory> svc, async_dispatcher_t* dispatcher,
                    adb_shell_config::Config config)
      : svc_(std::move(svc)), dispatcher_(dispatcher), config_(std::move(config)) {}

  // fuchsia_hardware_adb::Provider methods.
  void ConnectToService(ConnectToServiceRequestView request,
                        ConnectToServiceCompleter::Sync& completer) override;

  // For testing, return the current shell instance count.
  size_t ActiveShellInstances() {
    fbl::AutoLock _(&shell_lock_);
    return shells_.size();
  }

  // Create a new shell instance to service an incoming connect request.
  zx_status_t AddShell(std::optional<std::string> args, zx::socket client);

 private:
  // Destroy shell instance.
  void RemoveShell(AdbShellImpl* shell);

  fbl::Mutex shell_lock_;
  std::vector<std::unique_ptr<AdbShellImpl>> shells_ __TA_GUARDED(shell_lock_);

  fidl::ClientEnd<fuchsia_io::Directory> svc_;
  async_dispatcher_t* dispatcher_;
  adb_shell_config::Config config_;
};

// Class containing context of each shell instance.
class AdbShellImpl : public fidl::WireAsyncEventHandler<fuchsia_dash::Launcher> {
 public:
  explicit AdbShellImpl(fidl::UnownedClientEnd<fuchsia_io::Directory> svc,
                        async_dispatcher_t* dispatcher)
      : svc_(svc), dispatcher_(dispatcher) {}

  // Starts a dash shell with the help of dash launcher service. |moniker| is configured during
  // build time and is "./bootstrap/console-launcher" by default which provides a shell similar to
  // serial console. When the |adb| socket closes or when the user exits the shell, the dash
  // launcher will terminate the dash instance and sends an OnTerminate event which will eventually
  // result in dtor of AdbShellImpl getting called.
  zx_status_t Start(zx::socket adb, std::string moniker, std::optional<std::string> args,
                    fit::callback<void(AdbShellImpl*)> on_dead);

  // fuchsia_dash::Launcher event.
  void OnTerminated(fidl::WireEvent<fuchsia_dash::Launcher::OnTerminated>* event) override;

 private:
  fit::callback<void(AdbShellImpl*)> on_dead_ = [](auto adb_shell) {};

  fidl::UnownedClientEnd<fuchsia_io::Directory> svc_;

  fidl::WireSharedClient<fuchsia_dash::Launcher> dash_client_;
  async_dispatcher_t* dispatcher_;
};
}  // namespace adb_shell

#endif  // SRC_DEVELOPER_ADB_BIN_ADB_SHELL_ADB_SHELL_H_
