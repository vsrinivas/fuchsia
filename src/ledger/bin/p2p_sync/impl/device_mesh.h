// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_BIN_P2P_SYNC_IMPL_DEVICE_MESH_H_
#define SRC_LEDGER_BIN_P2P_SYNC_IMPL_DEVICE_MESH_H_

#include <memory>
#include <set>

#include "src/ledger/bin/p2p_provider/public/types.h"
#include "src/ledger/lib/convert/convert.h"

namespace p2p_sync {
// DeviceMesh is used by PageCommunicators to communicate with the device mesh.
class DeviceMesh {
 public:
  using DeviceSet = std::set<p2p_provider::P2PClientId>;

  DeviceMesh() = default;
  virtual ~DeviceMesh() = default;

  // Returns the current active device set.
  virtual DeviceSet GetDeviceList() = 0;

  // Sends the given buffer to a connected device.
  virtual void Send(const p2p_provider::P2PClientId& device_name,
                    convert::ExtendedStringView data) = 0;
};

}  // namespace p2p_sync

#endif  // SRC_LEDGER_BIN_P2P_SYNC_IMPL_DEVICE_MESH_H_
