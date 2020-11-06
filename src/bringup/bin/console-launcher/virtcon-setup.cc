// Copyright 2020 Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/bringup/bin/console-launcher/virtcon-setup.h"

#include <fuchsia/virtualconsole/llcpp/fidl.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/io.h>
#include <lib/fdio/spawn.h>
#include <lib/zircon-internal/paths.h>
#include <lib/zx/status.h>
#include <zircon/processargs.h>

namespace console_launcher {

namespace {
zx::status<zx::channel> StartShell(const char* cmd) {
  zx::channel local, remote;
  zx_status_t status = zx::channel::create(0, &local, &remote);
  if (status != ZX_OK) {
    return zx::error(status);
  }

  const char* argv[] = {ZX_SHELL_DEFAULT, nullptr, nullptr, nullptr};

  if (cmd) {
    argv[1] = "-c";
    argv[2] = cmd;
  }

  fdio_spawn_action_t actions[2] = {
      {.action = FDIO_SPAWN_ACTION_SET_NAME, .name = {.data = "vc:sh"}},
      {.action = FDIO_SPAWN_ACTION_ADD_HANDLE,
       .h = {.id = PA_HND(PA_FD, FDIO_FLAG_USE_FOR_STDIO), .handle = remote.release()}},
  };

  uint32_t flags = FDIO_SPAWN_CLONE_ALL & ~FDIO_SPAWN_CLONE_STDIO;

  char err_msg[FDIO_SPAWN_ERR_MSG_MAX_LENGTH];
  status = fdio_spawn_etc(ZX_HANDLE_INVALID, flags, argv[0], argv, nullptr, std::size(actions),
                          actions, nullptr, err_msg);
  if (status != ZX_OK) {
    printf("console-launcher: cannot spawn shell: %s: %d (%s)\n", err_msg, status,
           zx_status_get_string(status));
    return zx::error(status);
  }

  return zx::ok(std::move(local));
}

}  // namespace

zx_status_t SetupVirtcon(llcpp::fuchsia::boot::Arguments::SyncClient* boot_args) {
  llcpp::fuchsia::boot::BoolPair bool_keys[]{
      {fidl::StringView{"netsvc.disable"}, true},
      {fidl::StringView{"netsvc.netboot"}, false},
      {fidl::StringView{"devmgr.require-system"}, false},
  };
  auto bool_resp = boot_args->GetBools(fidl::unowned_vec(bool_keys));
  if (!bool_resp.ok()) {
    return bool_resp.status();
  }

  const bool netsvc_disable = bool_resp->values[0];
  const bool netsvc_netboot = bool_resp->values[1];
  const bool require_system = bool_resp->values[2];
  const bool netboot = !netsvc_disable && netsvc_netboot;

  const bool run_virtcon = !require_system || netboot;

  if (!run_virtcon) {
    return ZX_OK;
  }

  llcpp::fuchsia::virtualconsole::SessionManager::SyncClient virtcon;
  {
    zx::channel local, remote;
    zx_status_t status = zx::channel::create(0, &local, &remote);
    if (status != ZX_OK) {
      return status;
    }

    status = fdio_service_connect("/svc/fuchsia.virtualconsole.SessionManager", remote.release());
    if (status != ZX_OK) {
      fprintf(stderr, "console-launcher: unable to connect to %s: %d\n",
              llcpp::fuchsia::virtualconsole::SessionManager::Name, status);
      return status;
    }
    virtcon = llcpp::fuchsia::virtualconsole::SessionManager::SyncClient(std::move(local));
  }

  constexpr size_t kNumShells = 3;
  for (size_t i = 0; i < kNumShells; i++) {
    zx::channel shell;
    if (netboot && i == (kNumShells - 1)) {
      auto result = StartShell("dlog -f -t");
      if (!result.is_ok()) {
        fprintf(stderr, "console-launcher: unable start virtcon shell: %s\n",
                result.status_string());
        return result.status_value();
      }
      shell = std::move(result.value());
    } else {
      auto result = StartShell(nullptr);
      if (!result.is_ok()) {
        fprintf(stderr, "console-launcher: unable start virtcon shell: %s\n",
                result.status_string());
        return result.status_value();
      }
      shell = std::move(result.value());
    }

    {
      auto res = virtcon.CreateSession(std::move(shell));
      if (!res.ok()) {
        fprintf(stderr, "console-launcher: unable to create virtcon session: %d\n", res.status());
        return res.status();
      }
      if (res->status != ZX_OK) {
        fprintf(stderr, "console-launcher: unable to create virtcon session: %d\n", res.status());
        return res->status;
      }
    }
  }

  return ZX_OK;
}

}  // namespace console_launcher
