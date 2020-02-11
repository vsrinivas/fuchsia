// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#pragma once

#include <ddktl/device.h>
#include <ddktl/protocol/ethernet/mac.h>

namespace phy {

class PhyDevice;
using DeviceType = ddk::Device<PhyDevice, ddk::UnbindableNew>;

class PhyDevice : public DeviceType {
 public:
  explicit PhyDevice(zx_device_t* parent) : DeviceType(parent), eth_mac_(parent) {}

  static zx_status_t Create(void* ctx, zx_device_t* device);

  void DdkRelease();
  void DdkUnbindNew(ddk::UnbindTxn txn);

  zx_status_t ConfigPhy(const uint8_t mac[MAC_ARRAY_LENGTH]);

 private:
  ddk::EthMacProtocolClient eth_mac_;
};

}  // namespace phy
