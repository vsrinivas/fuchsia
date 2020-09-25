// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/sim/test/sim_test.h"

namespace wlan::brcmfmac {

// static
const std::vector<uint8_t> SimInterface::kDefaultScanChannels = {
    1,  2,   3,   4,   5,   6,   7,   8,   9,   10,  11,  32,  36,  40,  44,  48,  52,  56, 60,
    64, 100, 104, 108, 112, 116, 120, 124, 128, 132, 136, 140, 144, 149, 153, 157, 161, 165};

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

zx_status_t SimInterface::Init(std::shared_ptr<simulation::Environment> env,
                               wlan_info_mac_role_t role) {
  zx_status_t result = zx_channel_create(0, &ch_sme_, &ch_mlme_);
  if (result == ZX_OK) {
    env_ = env;
    role_ = role;
  }
  return result;
}

void SimInterface::OnAssocConf(const wlanif_assoc_confirm_t* resp) {
  ZX_ASSERT(assoc_ctx_.state == AssocContext::kAssociating);

  stats_.assoc_results.push_back(*resp);

  if (resp->result_code == WLAN_ASSOC_RESULT_SUCCESS) {
    assoc_ctx_.state = AssocContext::kAssociated;
    stats_.assoc_successes++;
  } else {
    assoc_ctx_.state = AssocContext::kNone;
  }
}

void SimInterface::OnAssocInd(const wlanif_assoc_ind_t* ind) {
  ZX_ASSERT(role_ == WLAN_INFO_MAC_ROLE_AP);
  stats_.assoc_indications.push_back(*ind);
}

void SimInterface::OnAuthInd(const wlanif_auth_ind_t* resp) {
  ZX_ASSERT(role_ == WLAN_INFO_MAC_ROLE_AP);
  stats_.auth_indications.push_back(*resp);
}

void SimInterface::OnAuthConf(const wlanif_auth_confirm_t* resp) {
  ZX_ASSERT(assoc_ctx_.state == AssocContext::kAuthenticating);
  ZX_ASSERT(!memcmp(assoc_ctx_.bssid.byte, resp->peer_sta_address, ETH_ALEN));

  stats_.auth_results.push_back(*resp);

  // We only support open authentication, for now
  ZX_ASSERT(resp->auth_type == WLAN_AUTH_TYPE_OPEN_SYSTEM);

  if (resp->result_code != WLAN_AUTH_RESULT_SUCCESS) {
    assoc_ctx_.state = AssocContext::kNone;
    return;
  }

  assoc_ctx_.state = AssocContext::kAssociating;

  //  Send assoc request
  wlanif_assoc_req_t assoc_req = {.rsne_len = 0, .vendor_ie_len = 0};
  memcpy(assoc_req.peer_sta_address, assoc_ctx_.bssid.byte, ETH_ALEN);
  if_impl_ops_->assoc_req(if_impl_ctx_, &assoc_req);
}

void SimInterface::OnChannelSwitch(const wlanif_channel_switch_info_t* ind) {
  stats_.csa_indications.push_back(*ind);
}

void SimInterface::OnDeauthConf(const wlanif_deauth_confirm_t* resp) {
  stats_.deauth_results.push_back(*resp);
}

void SimInterface::OnDeauthInd(const wlanif_deauth_indication_t* ind) {
  stats_.deauth_indications.push_back(*ind);
}

void SimInterface::OnDisassocInd(const wlanif_disassoc_indication_t* ind) {
  stats_.disassoc_indications.push_back(*ind);
}

void SimInterface::OnJoinConf(const wlanif_join_confirm_t* resp) {
  ZX_ASSERT(assoc_ctx_.state == AssocContext::kJoining);

  stats_.join_results.push_back(resp->result_code);

  if (resp->result_code != WLAN_JOIN_RESULT_SUCCESS) {
    assoc_ctx_.state = AssocContext::kNone;
    return;
  }

  assoc_ctx_.state = AssocContext::kAuthenticating;

  // Send auth request
  wlanif_auth_req_t auth_req;
  std::memcpy(auth_req.peer_sta_address, assoc_ctx_.bssid.byte, ETH_ALEN);
  auth_req.auth_type = WLAN_AUTH_TYPE_OPEN_SYSTEM;
  auth_req.auth_failure_timeout = 1000;  // ~1s (although value is ignored for now)
  if_impl_ops_->auth_req(if_impl_ctx_, &auth_req);
}

void SimInterface::OnScanEnd(const wlanif_scan_end_t* end) {
  auto results = scan_results_.find(end->txn_id);

  // Verify that we started a scan on this interface
  ZX_ASSERT(results != scan_results_.end());

  // Verify that the scan hasn't already received a completion notice
  ZX_ASSERT(!results->second.result_code);

  results->second.result_code = end->code;
}

void SimInterface::OnScanResult(const wlanif_scan_result_t* result) {
  auto results = scan_results_.find(result->txn_id);

  // Verify that we started a scan on this interface
  ZX_ASSERT(results != scan_results_.end());

  // Verify that the scan hasn't sent a completion notice
  ZX_ASSERT(!results->second.result_code);

  results->second.result_list.push_back(result->bss);
}

void SimInterface::OnStartConf(const wlanif_start_confirm_t* resp) {
  stats_.start_confirmations.push_back(*resp);
}

void SimInterface::OnStopConf(const wlanif_stop_confirm_t* resp) {
  stats_.stop_confirmations.push_back(*resp);
}

void SimInterface::Query(wlanif_query_info_t* out_info) {
  if_impl_ops_->query(if_impl_ctx_, out_info);
}

void SimInterface::GetMacAddr(common::MacAddr* out_macaddr) {
  wlanif_query_info_t info;
  Query(&info);
  memcpy(out_macaddr->byte, info.mac_addr, ETH_ALEN);
}

void SimInterface::StartAssoc(const common::MacAddr& bssid, const wlan_ssid_t& ssid,
                              const wlan_channel_t& channel) {
  // This should only be performed on a Client interface
  ZX_ASSERT(role_ == WLAN_INFO_MAC_ROLE_CLIENT);

  stats_.assoc_attempts++;

  // Save off context
  assoc_ctx_.state = AssocContext::kJoining;
  assoc_ctx_.bssid = bssid;
  assoc_ctx_.ssid = ssid;
  assoc_ctx_.channel = channel;

  // Send join request
  wlanif_join_req join_req = {};
  std::memcpy(join_req.selected_bss.bssid, bssid.byte, ETH_ALEN);
  join_req.selected_bss.ssid.len = ssid.len;
  memcpy(join_req.selected_bss.ssid.data, ssid.ssid, WLAN_MAX_SSID_LEN);
  join_req.selected_bss.chan = channel;
  join_req.selected_bss.bss_type = WLAN_BSS_TYPE_ANY_BSS;
  if_impl_ops_->join_req(if_impl_ctx_, &join_req);
}

void SimInterface::AssociateWith(const simulation::FakeAp& ap, std::optional<zx::duration> delay) {
  // This should only be performed on a Client interface
  ZX_ASSERT(role_ == WLAN_INFO_MAC_ROLE_CLIENT);

  common::MacAddr bssid = ap.GetBssid();
  wlan_ssid_t ssid = ap.GetSsid();
  wlan_channel_t channel = ap.GetChannel();

  if (delay) {
    SCHEDULE_CALL(*delay, &SimInterface::StartAssoc, this, bssid, ssid, channel);
  } else {
    StartAssoc(ap.GetBssid(), ap.GetSsid(), ap.GetChannel());
  }
}

void SimInterface::DeauthenticateFrom(const common::MacAddr& bssid, wlan_deauth_reason_t reason) {
  // This should only be performed on a Client interface
  ZX_ASSERT(role_ == WLAN_INFO_MAC_ROLE_CLIENT);

  wlanif_deauth_req_t deauth_req = {.reason_code = reason};
  memcpy(deauth_req.peer_sta_address, bssid.byte, ETH_ALEN);

  if_impl_ops_->deauth_req(if_impl_ctx_, &deauth_req);
}

void SimInterface::StartScan(uint64_t txn_id, bool active) {
  wlan_scan_type_t scan_type = active ? WLAN_SCAN_TYPE_ACTIVE : WLAN_SCAN_TYPE_PASSIVE;
  uint32_t dwell_time = active ? kDefaultActiveScanDwellTimeMs : kDefaultPassiveScanDwellTimeMs;
  size_t num_channels = kDefaultScanChannels.size();
  wlanif_scan_req_t req = {
      .txn_id = txn_id,
      .bss_type = WLAN_BSS_TYPE_INFRASTRUCTURE,
      .scan_type = scan_type,
      .num_channels = num_channels,
      .min_channel_time = dwell_time,
      .max_channel_time = dwell_time,
      .num_ssids = 0,
  };

  // Initialize the channel list
  ZX_ASSERT(num_channels <= WLAN_INFO_CHANNEL_LIST_MAX_CHANNELS);
  memcpy(req.channel_list, kDefaultScanChannels.data(), kDefaultScanChannels.size());
  memset(&req.channel_list[num_channels], 0, WLAN_INFO_CHANNEL_LIST_MAX_CHANNELS - num_channels);

  // Create an entry for tracking results
  ScanStatus scan_status;
  scan_results_.insert_or_assign(txn_id, scan_status);

  // Start the scan
  if_impl_ops_->start_scan(if_impl_ctx_, &req);
}

std::optional<wlan_scan_result_t> SimInterface::ScanResultCode(uint64_t txn_id) {
  auto results = scan_results_.find(txn_id);

  // Verify that we started a scan on this interface
  ZX_ASSERT(results != scan_results_.end());

  return results->second.result_code;
}

const std::list<wlanif_bss_description_t>* SimInterface::ScanResultBssList(uint64_t txn_id) {
  auto results = scan_results_.find(txn_id);

  // Verify that we started a scan on this interface
  ZX_ASSERT(results != scan_results_.end());

  return &results->second.result_list;
}

void SimInterface::StartSoftAp(const wlan_ssid_t& ssid, const wlan_channel_t& channel,
                               uint32_t beacon_period, uint32_t dtim_period) {
  // This should only be performed on an AP interface
  ZX_ASSERT(role_ == WLAN_INFO_MAC_ROLE_AP);

  wlanif_start_req_t start_req = {
      .bss_type = WLAN_BSS_TYPE_INFRASTRUCTURE,
      .beacon_period = beacon_period,
      .dtim_period = dtim_period,
      .channel = channel.primary,
      .rsne_len = 0,
  };

  // Set the SSID field in the request
  const size_t kSsidLen = sizeof(start_req.ssid.data);
  ZX_ASSERT(kSsidLen == sizeof(ssid.ssid));
  start_req.ssid.len = ssid.len;
  memcpy(start_req.ssid.data, ssid.ssid, kSsidLen);

  // Send request to driver
  if_impl_ops_->start_req(if_impl_ctx_, &start_req);

  // Remember context
  soft_ap_ctx_.ssid = ssid;

  // Return value is handled asynchronously in OnStartConf
}

void SimInterface::StopSoftAp() {
  // This should only be performed on an AP interface
  ZX_ASSERT(role_ == WLAN_INFO_MAC_ROLE_AP);

  wlanif_stop_req_t stop_req;

  ZX_ASSERT(sizeof(stop_req.ssid.data) == WLAN_MAX_SSID_LEN);
  // Use the ssid from the last call to StartSoftAp
  stop_req.ssid.len = soft_ap_ctx_.ssid.len;
  memcpy(stop_req.ssid.data, soft_ap_ctx_.ssid.ssid, WLAN_MAX_SSID_LEN);

  // Send request to driver
  if_impl_ops_->stop_req(if_impl_ctx_, &stop_req);
}

zx_status_t SimInterface::SetMulticastPromisc(bool enable) {
  return if_impl_ops_->set_multicast_promisc(if_impl_ctx_, enable);
}

SimTest::SimTest() {
  env_ = std::make_shared<simulation::Environment>();
  env_->AddStation(this);

  dev_mgr_ = std::make_unique<simulation::FakeDevMgr>();
  // The sim test is strictly a theoretical observer in the simulation environment thus it should be
  // able to see everything
  rx_sensitivity_ = std::numeric_limits<double>::lowest();
}

SimTest::~SimTest() {
  // Clean the ifaces created in test but not deleted.
  for (auto id : iface_id_set_) {
    if (device_->WlanphyImplDestroyIface(id) != ZX_OK) {
      BRCMF_ERR("Clean iface fail.\n");
    }
  }
  // Don't have to erase the iface ids here.
}

zx_status_t SimTest::PreInit() {
  // Allocate memory for a simulated device and register with dev_mgr
  return brcmfmac::SimDevice::Create(dev_mgr_->GetRootDevice(), dev_mgr_.get(), env_, &device_);
}

zx_status_t SimTest::Init() {
  zx_status_t status;

  // Allocate device and register with dev_mgr
  if (device_ == nullptr) {
    status = PreInit();
    if (status != ZX_OK) {
      return status;
    }
  }

  // Initialize device
  status = device_->Init();
  if (status != ZX_OK) {
    // Ownership of the device has been transferred to the dev_mgr, so we don't need to dealloc it
    device_ = nullptr;
  }
  return status;
}

zx_status_t SimTest::StartInterface(wlan_info_mac_role_t role, SimInterface* sim_ifc,
                                    std::optional<const wlanif_impl_ifc_protocol*> sme_protocol,
                                    std::optional<common::MacAddr> mac_addr) {
  zx_status_t status;
  if ((status = sim_ifc->Init(env_, role)) != ZX_OK) {
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

  if (!iface_id_set_.insert(sim_ifc->iface_id_).second) {
    BRCMF_ERR("Iface already exist in this test.\n");
    return ZX_ERR_ALREADY_EXISTS;
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

void SimTest::DeleteInterface(uint16_t iface_id) {
  auto iter = iface_id_set_.find(iface_id);
  if (iter == iface_id_set_.end()) {
    BRCMF_ERR("Iface id does not exist.\n");
    return;
  }
  ASSERT_EQ(device_->WlanphyImplDestroyIface(*iter), ZX_OK);
  iface_id_set_.erase(iter);
}

}  // namespace wlan::brcmfmac
