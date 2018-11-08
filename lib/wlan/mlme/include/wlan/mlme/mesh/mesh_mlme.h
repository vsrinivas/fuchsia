// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_WLAN_MLME_INCLUDE_WLAN_MLME_MESH_MESH_MLME_H_
#define GARNET_LIB_WLAN_MLME_INCLUDE_WLAN_MLME_MESH_MESH_MLME_H_

#include <wlan/common/buffer_reader.h>
#include <wlan/mlme/device_interface.h>
#include <wlan/mlme/mlme.h>

namespace wlan {

template <typename T> class MlmeMsg;

class MeshMlme : public Mlme {
   public:
    explicit MeshMlme(DeviceInterface* device);

    // Mlme interface methods.
    zx_status_t Init() override;
    zx_status_t HandleMlmeMsg(const BaseMlmeMsg& msg) override;
    zx_status_t HandleFramePacket(fbl::unique_ptr<Packet> pkt) override;
    zx_status_t HandleTimeout(const ObjectId id) override;

   private:
    const common::MacAddr& self_addr() const { return device_->GetState()->address(); }

    ::fuchsia::wlan::mlme::StartResultCodes Start(
        const MlmeMsg<::fuchsia::wlan::mlme::StartRequest>& req);
    void SendPeeringOpen(const MlmeMsg<::fuchsia::wlan::mlme::MeshPeeringOpenAction>& req);
    void SendPeeringConfirm(const MlmeMsg<::fuchsia::wlan::mlme::MeshPeeringConfirmAction>& req);
    void SendMgmtFrame(fbl::unique_ptr<Packet> packet);

    zx_status_t HandleAnyWlanFrame(fbl::unique_ptr<Packet> pkt);
    zx_status_t HandleAnyMgmtFrame(MgmtFrame<>&& frame);
    zx_status_t HandleActionFrame(common::MacAddr src_addr, BufferReader* r);
    zx_status_t HandleSelfProtectedAction(common::MacAddr src_addr, BufferReader* r);
    zx_status_t HandleMpmOpenAction(common::MacAddr src_addr, BufferReader* r);

    DeviceInterface* const device_;
    bool joined_ = false;
    Sequence seq_;
};

}  // namespace wlan

#endif  // GARNET_LIB_WLAN_MLME_INCLUDE_WLAN_MLME_MESH_MESH_MLME_H_
