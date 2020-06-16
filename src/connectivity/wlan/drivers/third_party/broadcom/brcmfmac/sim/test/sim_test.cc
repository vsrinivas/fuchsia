// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/sim/test/sim_test.h"

namespace wlan::brcmfmac {

// static
wlanif_impl_ifc_protocol_ops_t SimInterface::default_sme_dispatch_tbl_ = {
    // SME operations
    .on_scan_result =
        [](void* ctx, const wlanif_scan_result_t* result) {
          static_cast<SimInterface*>(ctx)->OnScanResult(result);
        },
    .on_scan_end =
        [](void* ctx, const wlanif_scan_end_t* end) {
          static_cast<SimInterface*>(ctx)->OnScanEnd(end);
        },
    .join_conf =
        [](void* ctx, const wlanif_join_confirm_t* resp) {
          static_cast<SimInterface*>(ctx)->OnJoinConf(resp);
        },
    .auth_conf =
        [](void* ctx, const wlanif_auth_confirm_t* resp) {
          static_cast<SimInterface*>(ctx)->OnAuthConf(resp);
        },
    .auth_ind =
        [](void* ctx, const wlanif_auth_ind_t* resp) {
          static_cast<SimInterface*>(ctx)->OnAuthInd(resp);
        },
    .deauth_conf =
        [](void* ctx, const wlanif_deauth_confirm_t* resp) {
          static_cast<SimInterface*>(ctx)->OnDeauthConf(resp);
        },
    .deauth_ind =
        [](void* ctx, const wlanif_deauth_indication_t* ind) {
          static_cast<SimInterface*>(ctx)->OnDeauthInd(ind);
        },
    .assoc_conf =
        [](void* ctx, const wlanif_assoc_confirm_t* resp) {
          static_cast<SimInterface*>(ctx)->OnAssocConf(resp);
        },
    .assoc_ind =
        [](void* ctx, const wlanif_assoc_ind_t* ind) {
          static_cast<SimInterface*>(ctx)->OnAssocInd(ind);
        },
    .disassoc_conf =
        [](void* ctx, const wlanif_disassoc_confirm_t* resp) {
          static_cast<SimInterface*>(ctx)->OnDisassocConf(resp);
        },
    .disassoc_ind =
        [](void* ctx, const wlanif_disassoc_indication_t* ind) {
          static_cast<SimInterface*>(ctx)->OnDisassocInd(ind);
        },
    .start_conf =
        [](void* ctx, const wlanif_start_confirm_t* resp) {
          static_cast<SimInterface*>(ctx)->OnStartConf(resp);
        },
    .stop_conf =
        [](void* ctx, const wlanif_stop_confirm_t* resp) {
          static_cast<SimInterface*>(ctx)->OnStopConf(resp);
        },
    .eapol_conf =
        [](void* ctx, const wlanif_eapol_confirm_t* resp) {
          static_cast<SimInterface*>(ctx)->OnEapolConf(resp);
        },
    .on_channel_switch =
        [](void* ctx, const wlanif_channel_switch_info_t* ind) {
          static_cast<SimInterface*>(ctx)->OnChannelSwitch(ind);
        },
    .signal_report =
        [](void* ctx, const wlanif_signal_report_indication_t* ind) {
          static_cast<SimInterface*>(ctx)->OnSignalReport(ind);
        },
    .eapol_ind =
        [](void* ctx, const wlanif_eapol_indication_t* ind) {
          static_cast<SimInterface*>(ctx)->OnEapolInd(ind);
        },
    .stats_query_resp =
        [](void* ctx, const wlanif_stats_query_response_t* resp) {
          static_cast<SimInterface*>(ctx)->OnStatsQueryResp(resp);
        },
    .relay_captured_frame =
        [](void* ctx, const wlanif_captured_frame_result_t* result) {
          static_cast<SimInterface*>(ctx)->OnRelayCapturedFrame(result);
        },
    .data_recv =
        [](void* ctx, const void* data, size_t data_size, uint32_t flags) {
          static_cast<SimInterface*>(ctx)->OnDataRecv(data, data_size, flags);
        },
};

// static
intptr_t SimTest::instance_num_ = 0;

SimTest::SimTest() {
  env_ = std::make_shared<simulation::Environment>();
  env_->AddStation(this);

  dev_mgr_ = std::make_shared<simulation::FakeDevMgr>();
  parent_dev_ = reinterpret_cast<zx_device_t*>(instance_num_++);
  // The sim test is strictly a theoretical observer in the simulation environment thus it should be
  // able to see everything
  rx_sensitivity_ = std::numeric_limits<double>::lowest();
}

zx_status_t SimTest::Init() {
  return brcmfmac::SimDevice::Create(parent_dev_, dev_mgr_, env_, &device_);
}

zx_status_t SimTest::StartInterface(wlan_info_mac_role_t role, SimInterface* sim_ifc,
                                    std::optional<const wlanif_impl_ifc_protocol*> sme_protocol,
                                    std::optional<common::MacAddr> mac_addr) {
  zx_status_t status;
  if ((status = sim_ifc->Init()) != ZX_OK) {
    return status;
  }

  wlanphy_impl_create_iface_req_t req = {
      .role = role,
      .sme_channel = sim_ifc->ch_mlme_,
      .has_init_mac_addr = mac_addr ? true : false,
  };
  if (mac_addr)
    memcpy(req.init_mac_addr, mac_addr.value().byte, ETH_ALEN);

  if ((status = device_->WlanphyImplCreateIface(&req, &sim_ifc->iface_id_)) != ZX_OK) {
    return status;
  }

  // This should have created a WLANIF_IMPL device
  auto device_info = dev_mgr_->FindFirstByProtocolId(ZX_PROTOCOL_WLANIF_IMPL);
  if (device_info == std::nullopt) {
    return ZX_ERR_INTERNAL;
  }

  sim_ifc->if_impl_ctx_ = device_info->dev_args.ctx;
  sim_ifc->if_impl_ops_ = static_cast<wlanif_impl_protocol_ops_t*>(device_info->dev_args.proto_ops);

  zx_handle_t sme_ch;
  if (!sme_protocol) {
    sme_protocol = &sim_ifc->default_ifc_;
  }
  status = sim_ifc->if_impl_ops_->start(sim_ifc->if_impl_ctx_, sme_protocol.value(), &sme_ch);

  // Verify that the channel passed back from start() is the same one we gave to create_iface()
  if (sme_ch != sim_ifc->ch_mlme_) {
    return ZX_ERR_INTERNAL;
  }

  return ZX_OK;
}

}  // namespace wlan::brcmfmac
