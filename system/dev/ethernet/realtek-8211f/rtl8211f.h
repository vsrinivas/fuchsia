// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once
#include <ddk/device.h>
#include <ddk/protocol/ethernet-mac.h>

namespace phy {

class PhyDevice {
public:
    zx_device_t* device_;
    static zx_status_t ConfigPhy(void* ctx, const uint8_t mac[MAC_ARRAY_LENGTH]);
    static zx_status_t Create(zx_device_t* device);

private:
    eth_mac_protocol_t eth_mac_;
};

} // namespace phy
