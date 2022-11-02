// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <fidl/fuchsia.hardware.dsi/cpp/wire.h>
#include <lib/fdio/directory.h>
#include <lib/fidl/cpp/wire/vector_view.h>
#include <lib/mipi-dsi/mipi-dsi.h>
#include <lib/zx/channel.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zircon/fidl.h>
#include <zircon/types.h>

#include <cstdint>
#include <cstdlib>

#include "src/lib/fxl/command_line.h"

namespace {

void usage(const std::string& argv0) {
  printf("\n%s [flags] [subcommand] [args]\n\n", argv0.c_str());
  printf("Subcommands:\n");
  printf("  on: Turns LCD on using DCS Command 0x29\n");
  printf("  off: Turns LCD off using DCS Command 0x28\n");
  printf("  brightness [power]: Sets the backlight power to [0, 255]\n");
  printf("Flags:\n");
  printf("  path: Path to dsi-base interface; typically contained in /dev/class/dsi-base/\n\n");
}

zx::result<uint32_t> ParseUintArg(const std::string& arg, uint32_t min, uint32_t max) {
  uint32_t out_val;
  bool is_hex = (arg[0] == '0') && (arg[1] == 'x');
  if (sscanf(arg.c_str(), is_hex ? "%x" : "%u", &out_val) != 1) {
    return zx::error(ZX_ERR_INVALID_ARGS);
  }

  if (out_val < min || out_val > max) {
    return zx::error(ZX_ERR_OUT_OF_RANGE);
  }

  return zx::ok(out_val);
}

namespace fidl_dsi = fuchsia_hardware_dsi;

}  // namespace

int main(int argc, char* argv[]) {
  // argv validation
  const fxl::CommandLine cmd = fxl::CommandLineFromArgcArgv(argc, argv);
  const auto& args = cmd.positional_args();
  std::string dev_path;
  if (cmd.GetOptionValue("path", &dev_path)) {
    printf("Using device %s\n", dev_path.c_str());
  } else {
    printf("No path provided\n");
    usage(cmd.argv0());
    return -1;
  }
  if (args.empty()) {
    printf("No subcommand provided\n");
    usage(cmd.argv0());
    return -1;
  }
  if (args[0] != "on" && args[0] != "off" && args[0] != "brightness") {
    printf("Invalid subcommand %s\n", args[0].c_str());
    usage(cmd.argv0());
    return -1;
  }
  if ((args[0] == "off" && args.size() != 1) || (args[0] == "on" && args.size() != 1) ||
      (args[0] == "brightness" && args.size() != 2)) {
    printf("Incorrect number of arguments\n");
    usage(cmd.argv0());
    return -1;
  }

  // connect to the DSI fidl service
  zx::channel local, remote;
  zx_status_t status = zx::channel::create(0, &local, &remote);
  if (status != ZX_OK) {
    printf("Could not create channel (%d)\n", status);
    return -1;
  }

  status = fdio_service_connect(dev_path.c_str(), remote.release());
  if (status != ZX_OK) {
    printf("Failed to connect to dsi-base %d\n", status);
    return -1;
  }
  fidl::WireSyncClient<fidl_dsi::DsiBase> client(std::move(local));

  // issue commands
  uint8_t tbuf[4] = {0, 0, 0, 0};
  uint32_t tlen = 0;
  bool is_dcs = true;

  if (args[0] == "off") {
    printf("Powering off the display\n");
    tbuf[0] = 0x28;
    tlen = 1;
  } else if (args[0] == "on") {
    printf("Powering on the display\n");
    tbuf[0] = 0x29;
    tlen = 1;
  } else if (args[0] == "brightness") {
    is_dcs = false;
    tbuf[0] = 0x29;
    tbuf[1] = 0x2;  // 2 follow-on bytes
    tbuf[2] = 0x51;

    auto power_or_status = ParseUintArg(args[1], 0, 255);
    if (power_or_status.is_error()) {
      printf("Failed to parse <brightness_power> %s: %s\n", args[1].c_str(),
             power_or_status.status_string());
      usage(cmd.argv0());
      return -1;
    }
    tbuf[3] = static_cast<uint8_t>(power_or_status.value());
    tlen = 4;
    printf("Setting display brightness to %d/255\n", tbuf[3]);
  }
  fidl::Arena<2048> allocator;
  std::optional res = mipi_dsi::MipiDsi::CreateCommandFidl(tlen, 0, is_dcs, allocator);
  if (!res.has_value()) {
    return -1;
  }
  auto response = client->SendCmd(res.value(), fidl::VectorView<uint8_t>::FromExternal(tbuf, tlen));

  if (!response.ok()) {
    printf("Could not send command to DSI (%s)\n", response.FormatDescription().c_str());
    return -1;
  }

  if (response->is_error()) {
    printf("Invalid Command Sent (%d)\n", response->error_value());
    return -1;
  }

  return 0;
}
