// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/sim/test/sim_test.h"

#include <fuchsia/wlan/common/c/banjo.h>
#include <fuchsia/wlan/ieee80211/cpp/fidl.h>
#include <fuchsia/wlan/internal/c/banjo.h>

namespace wlan_ieee80211 = ::fuchsia::wlan::ieee80211;

namespace wlan::brcmfmac {

// static
const std::vector<uint8_t> SimInterface::kDefaultScanChannels = {
    1,  2,   3,   4,   5,   6,   7,   8,   9,   10,  11,  32,  36,  40,  44,  48,  52,  56, 60,
    64, 100, 104, 108, 112, 116, 120, 124, 128, 132, 136, 140, 144, 149, 153, 157, 161, 165};

// static
wlan_fullmac_impl_ifc_protocol_ops_t SimInterface::default_sme_dispatch_tbl_ = {
    // SME operations
    .on_scan_result =
        [](void* ctx, const wlan_fullmac_scan_result_t* result) {
          static_cast<SimInterface*>(ctx)->OnScanResult(result);
        },
    .on_scan_end =
        [](void* ctx, const wlan_fullmac_scan_end_t* end) {
          static_cast<SimInterface*>(ctx)->OnScanEnd(end);
        },
    .connect_conf =
        [](void* ctx, const wlan_fullmac_connect_confirm_t* resp) {
          static_cast<SimInterface*>(ctx)->OnConnectConf(resp);
        },
    .auth_ind =
        [](void* ctx, const wlan_fullmac_auth_ind_t* resp) {
          static_cast<SimInterface*>(ctx)->OnAuthInd(resp);
        },
    .deauth_conf =
        [](void* ctx, const wlan_fullmac_deauth_confirm_t* resp) {
          static_cast<SimInterface*>(ctx)->OnDeauthConf(resp);
        },
    .deauth_ind =
        [](void* ctx, const wlan_fullmac_deauth_indication_t* ind) {
          static_cast<SimInterface*>(ctx)->OnDeauthInd(ind);
        },
    .assoc_ind =
        [](void* ctx, const wlan_fullmac_assoc_ind_t* ind) {
          static_cast<SimInterface*>(ctx)->OnAssocInd(ind);
        },
    .disassoc_conf =
        [](void* ctx, const wlan_fullmac_disassoc_confirm_t* resp) {
          static_cast<SimInterface*>(ctx)->OnDisassocConf(resp);
        },
    .disassoc_ind =
        [](void* ctx, const wlan_fullmac_disassoc_indication_t* ind) {
          static_cast<SimInterface*>(ctx)->OnDisassocInd(ind);
        },
    .start_conf =
        [](void* ctx, const wlan_fullmac_start_confirm_t* resp) {
          static_cast<SimInterface*>(ctx)->OnStartConf(resp);
        },
    .stop_conf =
        [](void* ctx, const wlan_fullmac_stop_confirm_t* resp) {
          static_cast<SimInterface*>(ctx)->OnStopConf(resp);
        },
    .eapol_conf =
        [](void* ctx, const wlan_fullmac_eapol_confirm_t* resp) {
          static_cast<SimInterface*>(ctx)->OnEapolConf(resp);
        },
    .on_channel_switch =
        [](void* ctx, const wlan_fullmac_channel_switch_info_t* ind) {
          static_cast<SimInterface*>(ctx)->OnChannelSwitch(ind);
        },
    .signal_report =
        [](void* ctx, const wlan_fullmac_signal_report_indication_t* ind) {
          static_cast<SimInterface*>(ctx)->OnSignalReport(ind);
        },
    .eapol_ind =
        [](void* ctx, const wlan_fullmac_eapol_indication_t* ind) {
          static_cast<SimInterface*>(ctx)->OnEapolInd(ind);
        },
    .relay_captured_frame =
        [](void* ctx, const wlan_fullmac_captured_frame_result_t* result) {
          static_cast<SimInterface*>(ctx)->OnRelayCapturedFrame(result);
        },
    .data_recv =
        [](void* ctx, const uint8_t* data, size_t data_size, uint32_t flags) {
          static_cast<SimInterface*>(ctx)->OnDataRecv(data, data_size, flags);
        },
};

zx_status_t SimInterface::Init(std::shared_ptr<simulation::Environment> env, wlan_mac_role_t role) {
  zx_status_t result = zx_channel_create(0, &ch_sme_, &ch_mlme_);
  if (result == ZX_OK) {
    env_ = env;
    role_ = role;
  }
  return result;
}

void SimInterface::OnConnectConf(const wlan_fullmac_connect_confirm_t* resp) {
  ZX_ASSERT(assoc_ctx_.state == AssocContext::kAssociating);

  stats_.connect_results.push_back(*resp);

  if (resp->result_code == STATUS_CODE_SUCCESS) {
    assoc_ctx_.state = AssocContext::kAssociated;
    stats_.connect_successes++;
  } else {
    assoc_ctx_.state = AssocContext::kNone;
  }
}

void SimInterface::OnAssocInd(const wlan_fullmac_assoc_ind_t* ind) {
  ZX_ASSERT(role_ == WLAN_MAC_ROLE_AP);
  stats_.assoc_indications.push_back(*ind);
}

void SimInterface::OnAuthInd(const wlan_fullmac_auth_ind_t* resp) {
  ZX_ASSERT(role_ == WLAN_MAC_ROLE_AP);
  stats_.auth_indications.push_back(*resp);
}

void SimInterface::OnChannelSwitch(const wlan_fullmac_channel_switch_info_t* ind) {
  stats_.csa_indications.push_back(*ind);
}

void SimInterface::OnDeauthConf(const wlan_fullmac_deauth_confirm_t* resp) {
  stats_.deauth_results.push_back(*resp);
}

void SimInterface::OnDeauthInd(const wlan_fullmac_deauth_indication_t* ind) {
  stats_.deauth_indications.push_back(*ind);
}

void SimInterface::OnDisassocInd(const wlan_fullmac_disassoc_indication_t* ind) {
  stats_.disassoc_indications.push_back(*ind);
}

void SimInterface::OnScanEnd(const wlan_fullmac_scan_end_t* end) {
  auto results = scan_results_.find(end->txn_id);

  // Verify that we started a scan on this interface
  ZX_ASSERT(results != scan_results_.end());

  // Verify that the scan hasn't already received a completion notice
  ZX_ASSERT(!results->second.result_code);

  results->second.result_code = end->code;
}

void SimInterface::OnScanResult(const wlan_fullmac_scan_result_t* result) {
  // Reassign to remove the const qualifier so we can change the BSS's IEs pointer later
  wlan_fullmac_scan_result_t copy = *result;
  auto results = scan_results_.find(copy.txn_id);

  // Verify that we started a scan on this interface
  ZX_ASSERT(results != scan_results_.end());

  // Verify that the scan hasn't sent a completion notice
  ZX_ASSERT(!results->second.result_code);

  // Copy the IES data over since the original location may change data by the time we verify.
  std::vector<uint8_t> ies(copy.bss.ies_list, copy.bss.ies_list + copy.bss.ies_count);
  scan_results_ies_.push_back(ies);
  copy.bss.ies_list = scan_results_ies_.at(scan_results_ies_.size() - 1).data();
  results->second.result_list.push_back(copy);
}

void SimInterface::OnStartConf(const wlan_fullmac_start_confirm_t* resp) {
  stats_.start_confirmations.push_back(*resp);
}

void SimInterface::OnStopConf(const wlan_fullmac_stop_confirm_t* resp) {
  stats_.stop_confirmations.push_back(*resp);
}

void SimInterface::StopInterface() { if_impl_ops_->stop(if_impl_ctx_); }

void SimInterface::Query(wlan_fullmac_query_info_t* out_info) {
  ZX_ASSERT(if_impl_ops_);
  if_impl_ops_->query(if_impl_ctx_, out_info);
}

void SimInterface::QueryMacSublayerSupport(mac_sublayer_support_t* out_resp) {
  ZX_ASSERT(if_impl_ops_);
  if_impl_ops_->query_mac_sublayer_support(if_impl_ctx_, out_resp);
}

void SimInterface::QuerySecuritySupport(security_support_t* out_resp) {
  ZX_ASSERT(if_impl_ops_);
  if_impl_ops_->query_security_support(if_impl_ctx_, out_resp);
}

void SimInterface::QuerySpectrumManagementSupport(spectrum_management_support_t* out_resp) {
  ZX_ASSERT(if_impl_ops_);
  if_impl_ops_->query_spectrum_management_support(if_impl_ctx_, out_resp);
}

void SimInterface::GetMacAddr(common::MacAddr* out_macaddr) {
  wlan_fullmac_query_info_t info;
  Query(&info);
  memcpy(out_macaddr->byte, info.sta_addr, ETH_ALEN);
}

void SimInterface::StartConnect(const common::MacAddr& bssid, const cssid_t& ssid,
                                const wlan_channel_t& channel) {
  ZX_ASSERT(if_impl_ops_);
  // This should only be performed on a Client interface
  ZX_ASSERT(role_ == WLAN_MAC_ROLE_CLIENT);

  stats_.connect_attempts++;

  // Save off context
  assoc_ctx_.state = AssocContext::kAssociating;
  assoc_ctx_.bssid = bssid;

  assoc_ctx_.ies.clear();
  assoc_ctx_.ies.push_back(0);         // SSID IE type ID
  assoc_ctx_.ies.push_back(ssid.len);  // SSID IE length
  assoc_ctx_.ies.insert(assoc_ctx_.ies.end(), ssid.data, ssid.data + ssid.len);
  assoc_ctx_.channel = channel;

  // Send connect request
  wlan_fullmac_connect_req connect_req = {};
  std::memcpy(connect_req.selected_bss.bssid, bssid.byte, ETH_ALEN);
  connect_req.selected_bss.ies_list = assoc_ctx_.ies.data();
  connect_req.selected_bss.ies_count = assoc_ctx_.ies.size();
  connect_req.selected_bss.channel = channel;
  connect_req.selected_bss.bss_type = BSS_TYPE_INFRASTRUCTURE;
  connect_req.auth_type = WLAN_AUTH_TYPE_OPEN_SYSTEM;
  connect_req.connect_failure_timeout = 1000;  // ~1s (although value is ignored for now)
  if_impl_ops_->connect_req(if_impl_ctx_, &connect_req);
}

void SimInterface::AssociateWith(const simulation::FakeAp& ap, std::optional<zx::duration> delay) {
  // This should only be performed on a Client interface
  ZX_ASSERT(role_ == WLAN_MAC_ROLE_CLIENT);

  common::MacAddr bssid = ap.GetBssid();
  cssid_t ssid = ap.GetSsid();
  wlan_channel_t channel = ap.GetChannel();

  if (delay) {
    env_->ScheduleNotification(std::bind(&SimInterface::StartConnect, this, bssid, ssid, channel),
                               *delay);
  } else {
    StartConnect(ap.GetBssid(), ap.GetSsid(), ap.GetChannel());
  }
}

void SimInterface::DeauthenticateFrom(const common::MacAddr& bssid, reason_code_t reason) {
  ZX_ASSERT(if_impl_ops_);
  // This should only be performed on a Client interface
  ZX_ASSERT(role_ == WLAN_MAC_ROLE_CLIENT);

  wlan_fullmac_deauth_req_t deauth_req = {.reason_code = reason};
  memcpy(deauth_req.peer_sta_address, bssid.byte, ETH_ALEN);

  if_impl_ops_->deauth_req(if_impl_ctx_, &deauth_req);
}

void SimInterface::StartScan(uint64_t txn_id, bool active,
                             std::optional<const std::vector<uint8_t>> channels_arg) {
  ZX_ASSERT(if_impl_ops_);
  wlan_scan_type_t scan_type = active ? WLAN_SCAN_TYPE_ACTIVE : WLAN_SCAN_TYPE_PASSIVE;
  uint32_t dwell_time = active ? kDefaultActiveScanDwellTimeMs : kDefaultPassiveScanDwellTimeMs;
  const std::vector<uint8_t> channels =
      channels_arg.has_value() ? channels_arg.value() : kDefaultScanChannels;

  wlan_fullmac_scan_req_t req = {
      .txn_id = txn_id,
      .scan_type = scan_type,
      .channels_list = channels.data(),
      .channels_count = channels.size(),
      .ssids_list = nullptr,
      .ssids_count = 0,
      .min_channel_time = dwell_time,
      .max_channel_time = dwell_time,
  };

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

const std::list<wlan_fullmac_scan_result_t>* SimInterface::ScanResultList(uint64_t txn_id) {
  auto results = scan_results_.find(txn_id);

  // Verify that we started a scan on this interface
  ZX_ASSERT(results != scan_results_.end());

  return &results->second.result_list;
}

void SimInterface::StartSoftAp(const cssid_t& ssid, const wlan_channel_t& channel,
                               uint32_t beacon_period, uint32_t dtim_period) {
  ZX_ASSERT(if_impl_ops_);
  // This should only be performed on an AP interface
  ZX_ASSERT(role_ == WLAN_MAC_ROLE_AP);

  wlan_fullmac_start_req_t start_req = {
      .bss_type = BSS_TYPE_INFRASTRUCTURE,
      .beacon_period = beacon_period,
      .dtim_period = dtim_period,
      .channel = channel.primary,
      .rsne_len = 0,
  };

  // Set the SSID field in the request
  const size_t kSsidLen = sizeof(start_req.ssid.data);
  ZX_ASSERT(kSsidLen == sizeof(ssid.data));
  start_req.ssid.len = ssid.len;
  memcpy(start_req.ssid.data, ssid.data, kSsidLen);

  // Send request to driver
  if_impl_ops_->start_req(if_impl_ctx_, &start_req);

  // Remember context
  soft_ap_ctx_.ssid = ssid;

  // Return value is handled asynchronously in OnStartConf
}

void SimInterface::StopSoftAp() {
  ZX_ASSERT(if_impl_ops_);
  // This should only be performed on an AP interface
  ZX_ASSERT(role_ == WLAN_MAC_ROLE_AP);

  wlan_fullmac_stop_req_t stop_req;

  ZX_ASSERT(sizeof(stop_req.ssid.data) == wlan_ieee80211::MAX_SSID_BYTE_LEN);
  // Use the ssid from the last call to StartSoftAp
  stop_req.ssid.len = soft_ap_ctx_.ssid.len;
  memcpy(stop_req.ssid.data, soft_ap_ctx_.ssid.data, wlan_ieee80211::MAX_SSID_BYTE_LEN);

  // Send request to driver
  if_impl_ops_->stop_req(if_impl_ctx_, &stop_req);
}

zx_status_t SimInterface::SetMulticastPromisc(bool enable) {
  ZX_ASSERT(if_impl_ops_);
  return if_impl_ops_->set_multicast_promisc(if_impl_ctx_, enable);
}

SimTest::SimTest() : test_arena_(nullptr) {
  env_ = std::make_shared<simulation::Environment>();
  env_->AddStation(this);

  dev_mgr_ = std::make_unique<simulation::FakeDevMgr>();
  // The sim test is strictly a theoretical observer in the simulation environment thus it should be
  // able to see everything
  rx_sensitivity_ = std::numeric_limits<double>::lowest();
}

SimTest::~SimTest() {
  // Clean the ifaces created in test but not deleted.
  for (auto iface : ifaces_) {
    fidl::Arena fidl_arena;
    auto builder =
        fuchsia_wlan_wlanphyimpl::wire::WlanphyImplDestroyIfaceRequest::Builder(fidl_arena);
    builder.iface_id(iface.first);
    auto result = client_.sync().buffer(test_arena_)->DestroyIface(builder.Build());
    if (!result.ok()) {
      BRCMF_ERR("Delete iface: %u failed", iface.first);
    }
    if (result->is_error()) {
      BRCMF_ERR("Delete iface: %u failed", iface.first);
    }
  }

  if (client_dispatcher_.get()) {
    client_dispatcher_.ShutdownAsync();
    completion_.Wait();
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
  status = device_->BusInit();
  if (status != ZX_OK) {
    // Ownership of the device has been transferred to the dev_mgr, so we don't need to dealloc it
    device_ = nullptr;
    return status;
  }

  // Create test arena.
  test_arena_ = fdf::Arena('TEST');

  // Create the FIDL endpoints, bind the client end to the test class, and the server end to
  // wlan::brcmfmac::Device class.
  auto endpoints = fdf::CreateEndpoints<fuchsia_wlan_wlanphyimpl::WlanphyImpl>();
  if (endpoints.is_error()) {
    BRCMF_ERR("Failed to create endpoints");
    return ZX_ERR_INTERNAL;
  }

  // Create a dispatcher to wait on the runtime channel.
  auto dispatcher =
      fdf::Dispatcher::Create(0, "sim-test", [&](fdf_dispatcher_t*) { completion_.Signal(); });

  if (dispatcher.is_error()) {
    BRCMF_ERR("Failed to create dispatcher : %s", zx_status_get_string(dispatcher.error_value()));
    return ZX_ERR_INTERNAL;
  }

  client_dispatcher_ = *std::move(dispatcher);
  client_ = fdf::WireSharedClient<fuchsia_wlan_wlanphyimpl::WlanphyImpl>(
      std::move(endpoints->client), client_dispatcher_.get());

  device_->DdkServiceConnect(fidl::DiscoverableProtocolName<fuchsia_wlan_wlanphyimpl::WlanphyImpl>,
                             endpoints->server.TakeHandle());
  return ZX_OK;
}

zx_status_t SimTest::StartInterface(
    wlan_mac_role_t role, SimInterface* sim_ifc,
    std::optional<const wlan_fullmac_impl_ifc_protocol*> sme_protocol,
    std::optional<common::MacAddr> mac_addr) {
  zx_status_t status;
  if ((status = sim_ifc->Init(env_, role)) != ZX_OK) {
    return status;
  }
  fuchsia_wlan_common::wire::WlanMacRole fidl_role;
  switch (role) {
    case WLAN_MAC_ROLE_CLIENT:
      fidl_role = fuchsia_wlan_common::wire::WlanMacRole::kClient;
      break;
    case WLAN_MAC_ROLE_AP:
      fidl_role = fuchsia_wlan_common::wire::WlanMacRole::kAp;
      break;
    case WLAN_MAC_ROLE_MESH:
      fidl_role = fuchsia_wlan_common::wire::WlanMacRole::kMesh;
      break;
    default:
      BRCMF_ERR("Invalid mac role %u", role);
      return ZX_ERR_INVALID_ARGS;
  }
  auto ch = zx::channel(sim_ifc->ch_mlme_);

  fidl::Arena fidl_arena;

  auto builder = fuchsia_wlan_wlanphyimpl::wire::WlanphyImplCreateIfaceRequest::Builder(fidl_arena);
  builder.role(fidl_role);
  builder.mlme_channel(std::move(ch));
  if (mac_addr) {
    fidl::Array<unsigned char, 6> init_sta_addr;
    memcpy(&init_sta_addr, mac_addr.value().byte, ETH_ALEN);
    builder.init_sta_addr(init_sta_addr);
  }

  auto result = client_.sync().buffer(test_arena_)->CreateIface(builder.Build());

  EXPECT_TRUE(result.ok());
  if (result->is_error()) {
    BRCMF_ERR("%s error happened while creating interface",
              zx_status_get_string(result->error_value()));
    return result->error_value();
  }
  sim_ifc->iface_id_ = result->value()->iface_id();

  status = ZX_OK;

  if (!ifaces_.insert_or_assign(sim_ifc->iface_id_, sim_ifc).second) {
    BRCMF_ERR("Iface already exist in this test.\n");
    return ZX_ERR_ALREADY_EXISTS;
  }

  // This should have created a WLAN_FULLMAC_IMPL device
  auto device = dev_mgr_->FindLatestByProtocolId(ZX_PROTOCOL_WLAN_FULLMAC_IMPL);
  if (device == nullptr) {
    return ZX_ERR_INTERNAL;
  }

  sim_ifc->if_impl_ctx_ = device->DevArgs().ctx;
  sim_ifc->if_impl_ops_ =
      static_cast<const wlan_fullmac_impl_protocol_ops_t*>(device->DevArgs().proto_ops);

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

zx_status_t SimTest::InterfaceDestroyed(SimInterface* ifc) {
  auto iter = ifaces_.find(ifc->iface_id_);

  if (iter == ifaces_.end()) {
    BRCMF_ERR("Iface id: %d does not exist", ifc->iface_id_);
    return ZX_ERR_NOT_FOUND;
  }

  ifaces_.erase(iter);

  return ZX_OK;
}

zx_status_t SimTest::DeleteInterface(SimInterface* ifc) {
  auto iter = ifaces_.find(ifc->iface_id_);

  if (iter == ifaces_.end()) {
    BRCMF_ERR("Iface id: %d does not exist", ifc->iface_id_);
    return ZX_ERR_NOT_FOUND;
  }

  BRCMF_DBG(SIM, "Del IF: %d", ifc->iface_id_);

  fidl::Arena fidl_arena;
  auto builder =
      fuchsia_wlan_wlanphyimpl::wire::WlanphyImplDestroyIfaceRequest::Builder(fidl_arena);
  builder.iface_id(iter->first);
  auto result = client_.sync().buffer(test_arena_)->DestroyIface(builder.Build());
  EXPECT_TRUE(result.ok());
  if (result->is_error()) {
    BRCMF_ERR("Failed to destroy interface.\n");
    return result->error_value();
  }

  // Once the interface data structures have been deleted, our pointers are no longer valid.
  ifc->if_impl_ctx_ = nullptr;
  ifc->if_impl_ops_ = nullptr;

  ifaces_.erase(iter);

  return ZX_OK;
}

}  // namespace wlan::brcmfmac
