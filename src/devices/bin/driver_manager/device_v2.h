// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BIN_DRIVER_MANAGER_DEVICE_V2_H_
#define SRC_DEVICES_BIN_DRIVER_MANAGER_DEVICE_V2_H_

#include "sdk/lib/driver_compat/device_server.h"
#include "src/devices/bin/driver_manager/v2/node.h"

namespace dfv2 {

// This class holds all the necessary things to run a DFv2 node that is backed by
// Device. The Device information is created in DriverManager's outgoing
// directory and routed to the Node.
class Device {
 public:
  static zx::result<std::unique_ptr<Device>> CreateAndServe(
      std::string topological_path, std::string name, uint64_t device_symbol,
      async_dispatcher_t* dispatcher, component::OutgoingDirectory* outgoing,
      compat::DeviceServer server, dfv2::NodeManager* manager, dfv2::DriverHost* driver_host);

  std::shared_ptr<dfv2::Node>& node() { return node_; }

 private:
  std::optional<compat::DeviceServer> server_;

  // This is the DFv2 node and driver.
  std::shared_ptr<dfv2::Node> node_;
};

}  // namespace dfv2

#endif  // SRC_DEVICES_BIN_DRIVER_MANAGER_DEVICE_V2_H_
