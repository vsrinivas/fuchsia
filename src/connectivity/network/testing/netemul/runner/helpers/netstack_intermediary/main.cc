// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/async/default.h>

#include <iostream>

#include "netstack_intermediary.h"
#include "src/lib/fxl/command_line.h"
#include "src/lib/fxl/logging.h"
#include "src/lib/syslog/cpp/logger.h"

constexpr size_t kMacNetworkNameLength = 100;

static void usage() {
  std::cerr << "Usage: netstack_intermediary --interface=0a:0b:0c:0d:0e:0f=net1" << std::endl;
  std::cerr << "                             --interface=0a:0b:0c:0d:0e:10=net2" << std::endl;
}

int main(int argc, char** argv) {
  auto command_line = fxl::CommandLineFromArgcArgv(argc, argv);

  syslog::InitLogger();

  if (!command_line.HasOption("interface")) {
    usage();
    return ZX_ERR_INVALID_ARGS;
  }

  std::map<std::array<uint8_t, 6>, std::string> mac_ethertap_mapping;
  for (const auto& interface_config : command_line.GetOptionValues("interface")) {
    // Verify that the "<MAC addr>=<network name>" will fit inside of the sscanf buffer.
    if (interface_config.size() > (kMacAddrStringLength + kMacNetworkNameLength + 1)) {
      FXL_LOG(ERROR) << "interface argument is too long: " << interface_config;
      return ZX_ERR_INVALID_ARGS;
    }

    // Allow room in the sscanf buffer for the ethertap network name and a null character.
    char ethertap_network[kMacNetworkNameLength + 1];
    std::array<uint8_t, 6> scan_mac;
    int32_t args_parsed = std::sscanf(
        interface_config.data(), "%02hhx:%02hhx:%02hhx:%02hhx:%02hhx:%02hhx=%s", &scan_mac[0],
        &scan_mac[1], &scan_mac[2], &scan_mac[3], &scan_mac[4], &scan_mac[5], ethertap_network);

    if (args_parsed != (scan_mac.size() + 1)) {
      FXL_LOG(ERROR) << "failed to parse interface config: " << interface_config;
      usage();
      return ZX_ERR_INVALID_ARGS;
    }
    if ((scan_mac[0] & 0x03) != 0x02) {
      FXL_LOG(ERROR) << "guest MAC cannot be multicast: " << interface_config;
      usage();
      return ZX_ERR_INVALID_ARGS;
    }

    mac_ethertap_mapping[scan_mac] = ethertap_network;
  }

  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  async_set_default_dispatcher(loop.dispatcher());
  NetstackIntermediary netstack_intermediary(std::move(mac_ethertap_mapping));
  loop.Run();
}
