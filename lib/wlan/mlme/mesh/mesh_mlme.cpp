// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <wlan/common/channel.h>
#include <wlan/mlme/beacon.h>
#include <wlan/mlme/device_caps.h>
#include <wlan/mlme/mesh/mesh_mlme.h>
#include <wlan/mlme/service.h>
#include <zircon/status.h>

namespace wlan {

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
    const uint8_t* rates = GetRatesByChannel(
        device->GetWlanInfo().ifc_info, channel.primary, &c.rates_len);
    static_assert(sizeof(SupportedRate) == sizeof(rates[0]));
    c.rates = reinterpret_cast<const SupportedRate*>(rates);
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
    cfg.tmpl.packet_head.len = packet->len();
    cfg.tmpl.packet_head.data = packet->mut_data();
    cfg.beacon_interval = req.body()->beacon_period;
    status = device_->EnableBeaconing(&cfg);
    if (status != ZX_OK) {
        errorf("[mesh-mlme] failed to enable beaconing: %s\n", zx_status_get_string(status));
        return wlan_mlme::StartResultCodes::INTERNAL_ERROR;
    }

    joined_ = true;
    return wlan_mlme::StartResultCodes::SUCCESS;
}

zx_status_t MeshMlme::HandleFramePacket(fbl::unique_ptr<Packet> pkt) {
    return ZX_OK;
}

zx_status_t MeshMlme::HandleTimeout(const ObjectId id) {
    return ZX_OK;
}

}  // namespace wlan
