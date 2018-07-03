// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_LEDGER_P2P_SYNC_IMPL_DEVICE_MESH_H_
#define PERIDOT_BIN_LEDGER_P2P_SYNC_IMPL_DEVICE_MESH_H_

#include <memory>
#include <set>

#include <lib/fxl/strings/string_view.h>

#include "peridot/lib/convert/convert.h"

namespace p2p_sync {
// DeviceMesh is used by PageCommunicators to communicate with the device mesh.
class DeviceMesh {
 public:
  using DeviceSet = std::set<std::string, convert::StringViewComparator>;

  DeviceMesh() {}
  virtual ~DeviceMesh() {}

  // Returns the current active device set.
  virtual const DeviceSet& GetDeviceList() = 0;

  // Sends the given buffer to a connected device.
  virtual void Send(fxl::StringView device_name, fxl::StringView data) = 0;
};

}  // namespace p2p_sync

#endif  // PERIDOT_BIN_LEDGER_P2P_SYNC_IMPL_DEVICE_MESH_H_
