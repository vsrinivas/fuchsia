// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <wlan/mlme/mlme.h>

namespace wlan {

class MeshMlme : public Mlme {
   public:
    explicit MeshMlme(DeviceInterface* device);

    // Mlme interface methods.
    zx_status_t Init() override;
    zx_status_t HandleMlmeMsg(const BaseMlmeMsg& msg) override;
    zx_status_t HandleFramePacket(fbl::unique_ptr<Packet> pkt) override;
    zx_status_t HandleTimeout(const ObjectId id) override;

   private:
    DeviceInterface* const device_ __attribute__((unused));
};

} // namespace wlan
