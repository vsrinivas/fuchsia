// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <wlan/mlme/mesh/mesh_mlme.h>

#include <fuchsia/wlan/mlme/c/fidl.h>
#include <wlan/common/channel.h>
#include <wlan/mlme/beacon.h>
#include <wlan/mlme/device_caps.h>
#include <wlan/mlme/mesh/parse_mp_action.h>
#include <wlan/mlme/mesh/write_mp_action.h>
#include <wlan/mlme/service.h>
#include <zircon/status.h>

namespace wlan {

static constexpr size_t kMaxMeshMgmtFrameSize = 1024;

namespace wlan_mlme = ::fuchsia::wlan::mlme;

static wlan_channel_t GetChannel(uint8_t requested_channel) {
    return wlan_channel_t{
        .primary = requested_channel,
        .cbw = CBW20,
    };
}

static MeshConfiguration GetMeshConfig() {
    MeshConfiguration mesh_config = {
        .active_path_sel_proto_id = MeshConfiguration::kHwmp,
        .active_path_sel_metric_id = MeshConfiguration::kAirtime,
        .congest_ctrl_method_id = MeshConfiguration::kCongestCtrlInactive,
        .sync_method_id = MeshConfiguration::kNeighborOffsetSync,
        .auth_proto_id = MeshConfiguration::kNoAuth,
    };

    mesh_config.mesh_capability.set_accepting_additional_peerings(1);
    mesh_config.mesh_capability.set_forwarding(1);
    return mesh_config;
}

static zx_status_t BuildMeshBeacon(wlan_channel_t channel, DeviceInterface* device,
                                   const MlmeMsg<wlan_mlme::StartRequest>& req,
                                   MgmtFrame<Beacon>* buffer, size_t* tim_ele_offset) {
    PsCfg ps_cfg;
    uint8_t dummy;
    auto mesh_config = GetMeshConfig();

    BeaconConfig c = {
        .bssid = device->GetState()->address(),
        .bss_type = BssType::kMesh,
        .ssid = &dummy,
        .ssid_len = 0,
        .rsne = nullptr,
        .rsne_len = 0,
        .beacon_period = req.body()->beacon_period,
        .channel = channel,
        .ps_cfg = &ps_cfg,
        .ht =
            {
                .ready = false,
            },
        .mesh_config = &mesh_config,
        .mesh_id = req.body()->mesh_id->data(),
        .mesh_id_len = req.body()->mesh_id->size(),
    };
    auto rates = GetRatesByChannel(device->GetWlanInfo().ifc_info, channel.primary);
    static_assert(sizeof(SupportedRate) == sizeof(rates[0]));
    c.rates = {reinterpret_cast<const SupportedRate*>(rates.data()), rates.size()};
    return BuildBeacon(c, buffer, tim_ele_offset);
}

MeshMlme::MeshMlme(DeviceInterface* device) : device_(device) {}

zx_status_t MeshMlme::Init() {
    return ZX_OK;
}

zx_status_t MeshMlme::HandleMlmeMsg(const BaseMlmeMsg& msg) {
    if (auto start_req = msg.As<wlan_mlme::StartRequest>()) {
        auto code = Start(*start_req);
        return service::SendStartConfirm(device_, code);
    } else if (auto mp_open = msg.As<wlan_mlme::MeshPeeringOpenAction>()) {
        SendPeeringOpen(*mp_open);
        return ZX_OK;
    } else if (auto mp_confirm = msg.As<wlan_mlme::MeshPeeringConfirmAction>()) {
        SendPeeringConfirm(*mp_confirm);
        return ZX_OK;
    } else {
        return ZX_ERR_NOT_SUPPORTED;
    }
}

wlan_mlme::StartResultCodes MeshMlme::Start(const MlmeMsg<wlan_mlme::StartRequest>& req) {
    if (joined_) { return wlan_mlme::StartResultCodes::BSS_ALREADY_STARTED_OR_JOINED; }

    wlan_channel_t channel = GetChannel(req.body()->channel);
    zx_status_t status = device_->SetChannel(channel);
    if (status != ZX_OK) {
        errorf("[mesh-mlme] failed to set channel to %s: %s\n", common::ChanStr(channel).c_str(),
               zx_status_get_string(status));
        return wlan_mlme::StartResultCodes::INTERNAL_ERROR;
    }

    MgmtFrame<Beacon> buffer;
    wlan_bcn_config_t cfg = {};
    status = BuildMeshBeacon(channel, device_, req, &buffer, &cfg.tim_ele_offset);
    if (status != ZX_OK) {
        errorf("[mesh-mlme] failed to build a beacon template: %s\n", zx_status_get_string(status));
        return wlan_mlme::StartResultCodes::INTERNAL_ERROR;
    }

    auto packet = buffer.Take();
    cfg.tmpl.packet_head.data_size = packet->len();
    cfg.tmpl.packet_head.data_buffer = packet->data();
    cfg.beacon_interval = req.body()->beacon_period;
    status = device_->EnableBeaconing(&cfg);
    if (status != ZX_OK) {
        errorf("[mesh-mlme] failed to enable beaconing: %s\n", zx_status_get_string(status));
        return wlan_mlme::StartResultCodes::INTERNAL_ERROR;
    }

    joined_ = true;
    return wlan_mlme::StartResultCodes::SUCCESS;
}

void MeshMlme::SendPeeringOpen(const MlmeMsg<wlan_mlme::MeshPeeringOpenAction>& req) {
    auto packet = GetWlanPacket(kMaxMeshMgmtFrameSize);
    if (packet == nullptr) { return; }

    BufferWriter w(*packet);
    WriteMpOpenActionFrame(&w, MacHeaderWriter { self_addr(), &seq_ }, *req.body());
    SendMgmtFrame(fbl::move(packet));
}

void MeshMlme::SendPeeringConfirm(const MlmeMsg<wlan_mlme::MeshPeeringConfirmAction>& req) {
    auto packet = GetWlanPacket(kMaxMeshMgmtFrameSize);
    if (packet == nullptr) { return; }

    BufferWriter w(*packet);
    WriteMpConfirmActionFrame(&w, MacHeaderWriter { self_addr(), &seq_ }, *req.body());
    SendMgmtFrame(fbl::move(packet));
}

void MeshMlme::SendMgmtFrame(fbl::unique_ptr<Packet> packet) {
    zx_status_t status = device_->SendWlan(fbl::move(packet), CBW20, WLAN_PHY_OFDM);
    if (status != ZX_OK) {
        errorf("[mesh-mlme] failed to send a mgmt frame: %s\n", zx_status_get_string(status));
    }
}

zx_status_t MeshMlme::HandleFramePacket(fbl::unique_ptr<Packet> pkt) {
    switch (pkt->peer()) {
    case Packet::Peer::kEthernet:
        break;
    case Packet::Peer::kWlan:
        return HandleAnyWlanFrame(fbl::move(pkt));
    default:
        errorf("unknown Packet peer: %u\n", pkt->peer());
        break;
    }
    return ZX_OK;
}

zx_status_t MeshMlme::HandleAnyWlanFrame(fbl::unique_ptr<Packet> pkt) {
    if (auto possible_mgmt_frame = MgmtFrameView<>::CheckType(pkt.get())) {
        if (auto mgmt_frame = possible_mgmt_frame.CheckLength()) {
            return HandleAnyMgmtFrame(mgmt_frame.IntoOwned(fbl::move(pkt)));
        }
    }
    return ZX_OK;
}

zx_status_t MeshMlme::HandleAnyMgmtFrame(MgmtFrame<>&& frame) {
    auto body = BufferReader(frame.View().body_data());

    switch (frame.hdr()->fc.subtype()) {
    case kAction:
        return HandleActionFrame(frame.hdr()->addr2, &body);
    default:
        return ZX_OK;
    }
}

zx_status_t MeshMlme::HandleActionFrame(common::MacAddr src_addr, BufferReader* r) {
    auto action_header = r->Read<ActionFrame>();
    if (action_header == nullptr) {
        return ZX_OK;
    }

    switch (action_header->category) {
    case to_enum_type(action::kSelfProtected):
        return HandleSelfProtectedAction(src_addr, r);
    default:
        return ZX_OK;
    }
}

zx_status_t MeshMlme::HandleSelfProtectedAction(common::MacAddr src_addr, BufferReader* r) {
    auto self_prot_header = r->Read<SelfProtectedActionHeader>();
    if (self_prot_header == nullptr) {
        return ZX_OK;
    }

    switch (self_prot_header->self_prot_action) {
    case action::kMeshPeeringOpen:
        return HandleMpmOpenAction(src_addr, r);
    default:
        return ZX_OK;
    }
}

zx_status_t MeshMlme::HandleMpmOpenAction(common::MacAddr src_addr, BufferReader* r) {
    wlan_mlme::MeshPeeringOpenAction action;
    if (!ParseMpOpenAction(r, &action)) {
        return ZX_OK;
    }

    src_addr.CopyTo(action.common.peer_sta_address.data());
    return SendServiceMsg(device_, &action, fuchsia_wlan_mlme_MLMEIncomingMpOpenActionOrdinal);
}

zx_status_t MeshMlme::HandleTimeout(const ObjectId id) {
    return ZX_OK;
}

}  // namespace wlan
