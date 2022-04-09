// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/bringup/bin/console-launcher/virtcon-setup.h"

#include <lib/fdio/directory.h>
#include <lib/fdio/io.h>
#include <lib/fdio/spawn.h>
#include <lib/service/llcpp/service.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/zircon-internal/paths.h>
#include <zircon/processargs.h>

namespace console_launcher {

namespace {

// Start a shell with a given command.
// If `cmd` is null, then the shell is launched interactively.
zx::status<fidl::ServerEnd<fuchsia_hardware_pty::Device>> StartShell(const char* cmd) {
  zx::status endpoints = fidl::CreateEndpoints<fuchsia_hardware_pty::Device>();
  if (endpoints.is_error()) {
    return endpoints.take_error();
  }

  const char* argv[] = {ZX_SHELL_DEFAULT, nullptr, nullptr, nullptr};

  if (cmd) {
    argv[1] = "-c";
    argv[2] = cmd;
  }

  fdio_spawn_action_t actions[] = {
      {
          .action = FDIO_SPAWN_ACTION_SET_NAME,
          .name =
              {
                  .data = "vc:sh",
              },
      },
      {
          .action = FDIO_SPAWN_ACTION_ADD_HANDLE,
          .h =
              {
                  .id = PA_HND(PA_FD, FDIO_FLAG_USE_FOR_STDIO),
                  .handle = endpoints->client.TakeChannel().release(),
              },
      },
  };

  constexpr uint32_t flags = FDIO_SPAWN_CLONE_ALL & ~FDIO_SPAWN_CLONE_STDIO;

  char err_msg[FDIO_SPAWN_ERR_MSG_MAX_LENGTH];
  if (zx_status_t status = fdio_spawn_etc(ZX_HANDLE_INVALID, flags, argv[0], argv, nullptr,
                                          std::size(actions), actions, nullptr, err_msg);
      status != ZX_OK) {
    FX_PLOGS(ERROR, status) << "failed to launch console shell: " << err_msg;
    return zx::error(status);
  }

  return zx::ok(std::move(endpoints->server));
}

// Start a shell with a given command, and send it to virtcon.
// If `cmd` is null, then the shell is launched interactively.
zx_status_t StartVirtconShell(
    const fidl::ClientEnd<fuchsia_virtualconsole::SessionManager>& virtcon, const char* cmd) {
  zx::status result = StartShell(cmd);
  if (result.is_error()) {
    FX_PLOGS(ERROR, result.status_value()) << "failed to start virtcon shell";
    return result.status_value();
  }

  const fidl::WireResult virtcon_result =
      fidl::WireCall(virtcon)->CreateSession(std::move(result.value()));
  if (!virtcon_result.ok()) {
    FX_PLOGS(ERROR, virtcon_result.status()) << "failed to create virtcon session";
    return virtcon_result.status();
  }
  const fidl::WireResponse virtcon_response = virtcon_result.value();
  if (virtcon_response.status != ZX_OK) {
    FX_PLOGS(ERROR, virtcon_response.status) << "failed to create virtcon session";
    return virtcon_response.status;
  }
  return ZX_OK;
}

}  // namespace

zx::status<VirtconArgs> GetVirtconArgs(const fidl::ClientEnd<fuchsia_boot::Arguments>& boot_args) {
  fuchsia_boot::wire::BoolPair bool_keys[]{
      {"netsvc.disable", true},
      {"netsvc.netboot", false},
      {"devmgr.require-system", false},
  };
  const fidl::WireResult bool_resp = fidl::WireCall(boot_args)->GetBools(
      fidl::VectorView<fuchsia_boot::wire::BoolPair>::FromExternal(bool_keys));
  if (!bool_resp.ok()) {
    return zx::error(bool_resp.status());
  }

  const bool netsvc_disable = bool_resp->values[0];
  const bool netsvc_netboot = bool_resp->values[1];
  const bool require_system = bool_resp->values[2];

  const bool netboot = !netsvc_disable && netsvc_netboot;
  const bool should_launch = !require_system || netboot;

  return zx::ok(VirtconArgs{
      .should_launch = should_launch,
      .need_debuglog = netboot,
  });
}

zx_status_t SetupVirtconEtc(const fidl::ClientEnd<fuchsia_virtualconsole::SessionManager>& virtcon,
                            const VirtconArgs& args) {
  if (!args.should_launch) {
    return ZX_OK;
  }

  constexpr size_t kNumShells = 3;
  for (size_t i = 0; i < kNumShells; i++) {
    zx_status_t status;
    if (args.need_debuglog && (i == 0)) {
      status = StartVirtconShell(virtcon, "dlog -f -t");
    } else {
      status = StartVirtconShell(virtcon, nullptr);
    }
    if (status != ZX_OK) {
      return status;
    }
  }

  return ZX_OK;
}

zx_status_t SetupVirtcon(const fidl::ClientEnd<fuchsia_boot::Arguments>& boot_args) {
  const zx::status result = GetVirtconArgs(boot_args);
  if (result.is_error()) {
    return result.status_value();
  }
  VirtconArgs args = result.value();

  if (!args.should_launch) {
    return ZX_OK;
  }

  zx::status virtcon = service::Connect<fuchsia_virtualconsole::SessionManager>();
  if (virtcon.is_error()) {
    FX_PLOGS(ERROR, virtcon.status_value())
        << "failed to connect to "
        << fidl::DiscoverableProtocolName<fuchsia_virtualconsole::SessionManager>;
    return virtcon.status_value();
  }

  return SetupVirtconEtc(virtcon.value(), args);
}

}  // namespace console_launcher
