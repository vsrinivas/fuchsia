// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>

#include <iostream>

#include "netstack_intermediary.h"
#include "src/lib/fxl/command_line.h"
#include "src/lib/fxl/logging.h"
#include "src/lib/syslog/cpp/logger.h"

constexpr size_t kMacNetworkNameLength = 100;
constexpr size_t kMacAddrStringLength = 17;

static void usage() {
  std::cerr << "Usage: netstack_intermediary --network=<virtual_network_name>" << std::endl;
  std::cerr << "       netstack_intermediary --interface=01:02:03:04:05:06=net1" << std::endl;
  std::cerr << "                             --interface=0a:0b:0c:0d:0e:0f=net2" << std::endl;
}

int main(int argc, char** argv) {
  auto command_line = fxl::CommandLineFromArgcArgv(argc, argv);

  syslog::InitLogger();

  if ((!command_line.HasOption("network") && !command_line.HasOption("interface")) ||
      (command_line.HasOption("network") && command_line.HasOption("interface"))) {
    FXL_LOG(ERROR) << "specify either --network or repeated --interface";
    usage();
    return ZX_ERR_INVALID_ARGS;
  }

  std::string network_name;
  std::map<std::array<uint8_t, 6>, std::string> mac_ethertap_mapping;
  if (command_line.HasOption("network")) {
    command_line.GetOptionValue("network", &network_name);
  } else {
    for (const auto& interface_config : command_line.GetOptionValues("interface")) {
      // Verify that the "<MAC addr>=<network name>" will fit inside of the sscanf buffer.
      if (interface_config.size() > (kMacAddrStringLength + kMacNetworkNameLength + 1)) {
        FXL_LOG(ERROR) << "interface argument is too long: " << interface_config;
        return ZX_ERR_INVALID_ARGS;
      }

      // Allow room in the sscanf buffer for the ethertap network name and a null character.
      char ethertap_network[kMacNetworkNameLength + 1];
      std::array<uint32_t, 6> scan_mac;
      int32_t args_parsed = std::sscanf(interface_config.data(), "%02x:%02x:%02x:%02x:%02x:%02x=%s",
                                        &scan_mac[0], &scan_mac[1], &scan_mac[2], &scan_mac[3],
                                        &scan_mac[4], &scan_mac[5], ethertap_network);

      if (args_parsed != 7) {
        FXL_LOG(ERROR) << "failed to parse interface config: " << interface_config;
        usage();
        return ZX_ERR_INVALID_ARGS;
      }

      std::array<uint8_t, 6> guest_mac;
      for (uint8_t i = 0; i < scan_mac.size(); i++) {
        guest_mac[i] = static_cast<uint8_t>(scan_mac[i]);
      }
      mac_ethertap_mapping[guest_mac] = ethertap_network;
    }
  }

  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  async_set_default_dispatcher(loop.dispatcher());
  NetstackIntermediary netstack_intermediary(std::move(network_name),
                                             std::move(mac_ethertap_mapping));
  loop.Run();
}
