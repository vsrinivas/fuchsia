// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <wlan/mlme/mesh/mesh_mlme.h>

namespace wlan {

namespace wlan_mlme = ::fuchsia::wlan::mlme;

MeshMlme::MeshMlme(DeviceInterface* device) : device_(device) {}

zx_status_t MeshMlme::Init() {
    return ZX_OK;
}

zx_status_t MeshMlme::HandleMlmeMsg(const BaseMlmeMsg& msg) {
    return ZX_OK;
}

zx_status_t MeshMlme::HandleFramePacket(fbl::unique_ptr<Packet> pkt) {
    return ZX_OK;
}

zx_status_t MeshMlme::HandleTimeout(const ObjectId id) {
    return ZX_OK;
}

} // namespace wlan
