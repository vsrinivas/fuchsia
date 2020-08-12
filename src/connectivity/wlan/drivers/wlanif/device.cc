// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "device.h"

#include <lib/async/cpp/task.h>
#include <net/ethernet.h>
#include <zircon/status.h>

#include <ddk/device.h>
#include <ddk/hw/wlan/wlaninfo.h>
#include <wlan/common/logging.h>

#include "convert.h"
#include "ddk/protocol/wlanif.h"
#include "driver.h"
#include "fuchsia/wlan/mlme/cpp/fidl.h"

namespace wlanif {

namespace wlan_common = ::fuchsia::wlan::common;
namespace wlan_mlme = ::fuchsia::wlan::mlme;
namespace wlan_stats = ::fuchsia::wlan::stats;

Device::Device(zx_device_t* device, wlanif_impl_protocol_t wlanif_impl_proto)
    : parent_(device),
      wlanif_impl_(wlanif_impl_proto),
      loop_(&kAsyncLoopConfigNoAttachToCurrentThread),
      binding_(this) {
  debugfn();
}

Device::~Device() { debugfn(); }

#define DEV(c) static_cast<Device*>(c)
static zx_protocol_device_t eth_device_ops = {
    .version = DEVICE_OPS_VERSION,
    .unbind = [](void* ctx) { DEV(ctx)->EthUnbind(); },
    .release = [](void* ctx) { DEV(ctx)->EthRelease(); },
};

static wlanif_impl_ifc_protocol_ops_t wlanif_impl_ifc_ops = {
    // MLME operations
    .on_scan_result = [](void* cookie,
                         const wlanif_scan_result_t* result) { DEV(cookie)->OnScanResult(result); },
    .on_scan_end = [](void* cookie, const wlanif_scan_end_t* end) { DEV(cookie)->OnScanEnd(end); },
    .join_conf = [](void* cookie,
                    const wlanif_join_confirm_t* resp) { DEV(cookie)->JoinConf(resp); },
    .auth_conf = [](void* cookie,
                    const wlanif_auth_confirm_t* resp) { DEV(cookie)->AuthenticateConf(resp); },
    .auth_ind = [](void* cookie,
                   const wlanif_auth_ind_t* ind) { DEV(cookie)->AuthenticateInd(ind); },
    .deauth_conf =
        [](void* cookie, const wlanif_deauth_confirm_t* resp) {
          DEV(cookie)->DeauthenticateConf(resp);
        },
    .deauth_ind =
        [](void* cookie, const wlanif_deauth_indication_t* ind) {
          DEV(cookie)->DeauthenticateInd(ind);
        },
    .assoc_conf = [](void* cookie,
                     const wlanif_assoc_confirm_t* resp) { DEV(cookie)->AssociateConf(resp); },
    .assoc_ind = [](void* cookie,
                    const wlanif_assoc_ind_t* ind) { DEV(cookie)->AssociateInd(ind); },
    .disassoc_conf =
        [](void* cookie, const wlanif_disassoc_confirm_t* resp) {
          DEV(cookie)->DisassociateConf(resp);
        },
    .disassoc_ind =
        [](void* cookie, const wlanif_disassoc_indication_t* ind) {
          DEV(cookie)->DisassociateInd(ind);
        },
    .start_conf = [](void* cookie,
                     const wlanif_start_confirm_t* resp) { DEV(cookie)->StartConf(resp); },
    .stop_conf = [](void* cookie,
                    const wlanif_stop_confirm_t* resp) { DEV(cookie)->StopConf(resp); },
    .eapol_conf = [](void* cookie,
                     const wlanif_eapol_confirm_t* resp) { DEV(cookie)->EapolConf(resp); },
    .on_channel_switch =
        [](void* cookie, const wlanif_channel_switch_info_t* ind) {
          DEV(cookie)->OnChannelSwitched(ind);
        },
    // MLME extension operations
    .signal_report =
        [](void* cookie, const wlanif_signal_report_indication_t* ind) {
          DEV(cookie)->SignalReport(ind);
        },
    .eapol_ind = [](void* cookie,
                    const wlanif_eapol_indication_t* ind) { DEV(cookie)->EapolInd(ind); },
    .stats_query_resp =
        [](void* cookie, const wlanif_stats_query_response_t* resp) {
          DEV(cookie)->StatsQueryResp(resp);
        },
    .relay_captured_frame =
        [](void* cookie, const wlanif_captured_frame_result_t* result) {
          DEV(cookie)->RelayCapturedFrame(result);
        },

    // Ethernet operations
    .data_recv = [](void* cookie, const void* data, size_t length,
                    uint32_t flags) { DEV(cookie)->EthRecv(data, length, flags); },
};

static ethernet_impl_protocol_ops_t ethernet_impl_ops = {
    .query = [](void* ctx, uint32_t options, ethernet_info_t* info) -> zx_status_t {
      return DEV(ctx)->EthQuery(options, info);
    },
    .stop = [](void* ctx) { DEV(ctx)->EthStop(); },
    .start = [](void* ctx, const ethernet_ifc_protocol_t* ifc) -> zx_status_t {
      return DEV(ctx)->EthStart(ifc);
    },
    .queue_tx =
        [](void* ctx, uint32_t options, ethernet_netbuf_t* netbuf,
           ethernet_impl_queue_tx_callback completion_cb,
           void* cookie) { return DEV(ctx)->EthQueueTx(options, netbuf, completion_cb, cookie); },
    .set_param = [](void* ctx, uint32_t param, int32_t value, const void* data, size_t data_size)
        -> zx_status_t { return DEV(ctx)->EthSetParam(param, value, data, data_size); },
};
#undef DEV

zx_status_t Device::AddEthDevice() {
  device_add_args_t args = {};
  args.version = DEVICE_ADD_ARGS_VERSION;
  args.name = "wlan-ethernet";
  args.ctx = this;
  args.ops = &eth_device_ops;
  args.proto_id = ZX_PROTOCOL_ETHERNET_IMPL;
  args.proto_ops = &ethernet_impl_ops;
  return device_add(parent_, &args, &ethdev_);
}

#define VERIFY_PROTO_OP(fn)                                           \
  do {                                                                \
    if (wlanif_impl_.ops->fn == nullptr) {                            \
      errorf("wlanif: required protocol function %s missing\n", #fn); \
      return ZX_ERR_INVALID_ARGS;                                     \
    }                                                                 \
  } while (0)

zx_status_t Device::Bind() {
  debugfn();

  // Assert minimum required functionality from the wlanif_impl driver
  if (wlanif_impl_.ops == nullptr) {
    errorf("wlanif: no wlanif_impl protocol ops provided\n");
    return ZX_ERR_INVALID_ARGS;
  }
  VERIFY_PROTO_OP(start);
  VERIFY_PROTO_OP(query);
  VERIFY_PROTO_OP(start_scan);
  VERIFY_PROTO_OP(join_req);
  VERIFY_PROTO_OP(auth_req);
  VERIFY_PROTO_OP(auth_resp);
  VERIFY_PROTO_OP(deauth_req);
  VERIFY_PROTO_OP(assoc_req);
  VERIFY_PROTO_OP(assoc_resp);
  VERIFY_PROTO_OP(disassoc_req);
  VERIFY_PROTO_OP(reset_req);
  VERIFY_PROTO_OP(start_req);
  VERIFY_PROTO_OP(stop_req);
  VERIFY_PROTO_OP(set_keys_req);
  VERIFY_PROTO_OP(del_keys_req);
  VERIFY_PROTO_OP(eapol_req);

  // The MLME interface has no start/stop commands, so we will start the wlanif_impl
  // device immediately
  zx_handle_t sme_channel = ZX_HANDLE_INVALID;
  zx_status_t status = wlanif_impl_start(&wlanif_impl_, this, &wlanif_impl_ifc_ops, &sme_channel);
  ZX_DEBUG_ASSERT(sme_channel != ZX_HANDLE_INVALID);
  if (status != ZX_OK) {
    errorf("wlanif: call to wlanif-impl start() failed: %s\n", zx_status_get_string(status));
    return status;
  }

  // Query the device.
  wlanif_impl_query(&wlanif_impl_, &query_info_);

  status = loop_.StartThread("wlanif-loop");
  if (status != ZX_OK) {
    errorf("wlanif: unable to start async loop: %s\n", zx_status_get_string(status));
    return status;
  }

  ZX_DEBUG_ASSERT(ethdev_ == nullptr);
  status = AddEthDevice();

  if (status != ZX_OK) {
    errorf("wlanif: could not add ethernet_impl device: %s\n", zx_status_get_string(status));
  } else {
    status = Connect(zx::channel(sme_channel));
    if (status != ZX_OK) {
      errorf("wlanif: unable to wait on SME channel: %s\n", zx_status_get_string(status));
      device_async_remove(ethdev_);
      return status;
    }
  }

  if (status != ZX_OK) {
    loop_.Shutdown();
  }
  return status;
}
#undef VERIFY_PROTO_OP

void Device::EthUnbind() {
  debugfn();
  // Stop accepting new FIDL requests.
  std::lock_guard<std::mutex> lock(lock_);
  if (binding_.is_bound()) {
    binding_.Unbind();
  }

  // Ensure that all FIDL messages have been processed before removing the device
  auto dispatcher = loop_.dispatcher();
  ::async::PostTask(dispatcher, [this] { device_unbind_reply(ethdev_); });
}

void Device::EthRelease() {
  debugfn();
  delete this;
}

zx_status_t Device::Connect(zx::channel request) {
  debugfn();
  std::lock_guard<std::mutex> lock(lock_);
  if (binding_.is_bound()) {
    return ZX_ERR_ALREADY_BOUND;
  }
  return binding_.Bind(std::move(request), loop_.dispatcher());
}

void Device::StartScan(wlan_mlme::ScanRequest req) {
  wlanif_scan_req_t impl_req = {};

  // txn_id
  impl_req.txn_id = req.txn_id;

  // bss_type
  impl_req.bss_type = ConvertBSSType(req.bss_type);

  // bssid
  std::memcpy(impl_req.bssid, req.bssid.data(), ETH_ALEN);

  // ssid
  CopySSID(req.ssid, &impl_req.ssid);

  // scan_type
  impl_req.scan_type = ConvertScanType(req.scan_type);

  // probe_delay
  impl_req.probe_delay = req.probe_delay;

  // channel_list
  std::vector<uint8_t> channel_list;
  if (req.channel_list.has_value()) {
    channel_list = std::move(req.channel_list.value());
  }
  if (channel_list.size() > WLAN_INFO_CHANNEL_LIST_MAX_CHANNELS) {
    warnf("wlanif: truncating channel list from %lu to %d\n", channel_list.size(),
          WLAN_INFO_CHANNEL_LIST_MAX_CHANNELS);
    impl_req.num_channels = WLAN_INFO_CHANNEL_LIST_MAX_CHANNELS;
  } else {
    impl_req.num_channels = channel_list.size();
  }
  std::memcpy(impl_req.channel_list, channel_list.data(), impl_req.num_channels);

  // min_channel_time
  impl_req.min_channel_time = req.min_channel_time;

  // max_channel_time
  impl_req.max_channel_time = req.max_channel_time;

  // ssid_list
  std::vector<std::vector<uint8_t>> ssid_list;
  if (req.ssid_list.has_value()) {
    ssid_list = std::move(req.ssid_list.value());
  }
  size_t num_ssids = ssid_list.size();
  if (num_ssids > WLAN_SCAN_MAX_SSIDS) {
    warnf("wlanif: truncating SSID list from %zu to %d\n", num_ssids, WLAN_SCAN_MAX_SSIDS);
    num_ssids = WLAN_SCAN_MAX_SSIDS;
  }
  for (size_t ndx = 0; ndx < num_ssids; ndx++) {
    CopySSID(ssid_list[ndx], &impl_req.ssid_list[ndx]);
  }
  impl_req.num_ssids = num_ssids;

  wlanif_impl_start_scan(&wlanif_impl_, &impl_req);
}

void Device::JoinReq(wlan_mlme::JoinRequest req) {
  SetEthernetStatusUnlocked(false);

  wlanif_join_req_t impl_req = {};

  // selected_bss
  ConvertBSSDescription(&impl_req.selected_bss, req.selected_bss);

  // join_failure_timeout
  impl_req.join_failure_timeout = req.join_failure_timeout;

  // nav_sync_delay
  impl_req.nav_sync_delay = req.nav_sync_delay;

  // op_rates
  if (req.op_rates.size() > WLAN_MAX_OP_RATES) {
    warnf("wlanif: truncating operational rates set from %zu to %d members\n", req.op_rates.size(),
          WLAN_MAX_OP_RATES);
    impl_req.num_op_rates = WLAN_MAX_OP_RATES;
  } else {
    impl_req.num_op_rates = req.op_rates.size();
  }
  std::memcpy(impl_req.op_rates, req.op_rates.data(), impl_req.num_op_rates);

  wlanif_impl_join_req(&wlanif_impl_, &impl_req);
}

void Device::AuthenticateReq(wlan_mlme::AuthenticateRequest req) {
  SetEthernetStatusUnlocked(false);

  wlanif_auth_req_t impl_req = {};

  // peer_sta_address
  std::memcpy(impl_req.peer_sta_address, req.peer_sta_address.data(), ETH_ALEN);

  // auth_type
  impl_req.auth_type = ConvertAuthType(req.auth_type);

  // auth_failure_timeout
  impl_req.auth_failure_timeout = req.auth_failure_timeout;

  wlanif_impl_auth_req(&wlanif_impl_, &impl_req);
}

void Device::AuthenticateResp(wlan_mlme::AuthenticateResponse resp) {
  wlanif_auth_resp_t impl_resp = {};

  // peer_sta_address
  std::memcpy(impl_resp.peer_sta_address, resp.peer_sta_address.data(), ETH_ALEN);

  // result_code
  impl_resp.result_code = ConvertAuthResultCode(resp.result_code);

  wlanif_impl_auth_resp(&wlanif_impl_, &impl_resp);
}

void Device::DeauthenticateReq(wlan_mlme::DeauthenticateRequest req) {
  SetEthernetStatusUnlocked(false);

  wlanif_deauth_req_t impl_req = {};

  // peer_sta_address
  std::memcpy(impl_req.peer_sta_address, req.peer_sta_address.data(), ETH_ALEN);

  // reason_code
  impl_req.reason_code = ConvertDeauthReasonCode(req.reason_code);

  wlanif_impl_deauth_req(&wlanif_impl_, &impl_req);
}

void Device::AssociateReq(wlan_mlme::AssociateRequest req) {
  wlanif_assoc_req_t impl_req = {};

  // peer_sta_address
  std::memcpy(impl_req.peer_sta_address, req.peer_sta_address.data(), ETH_ALEN);
  {
    std::lock_guard<std::mutex> lock(lock_);
    protected_bss_ = req.rsne.has_value() || req.vendor_ies.has_value();

    // rsne
    if (protected_bss_) {
      if (req.rsne.has_value()) {
        CopyRSNE(req.rsne.value(), impl_req.rsne, &impl_req.rsne_len);
      }
      if (req.vendor_ies.has_value()) {
        CopyVendorSpecificIE(req.vendor_ies.value(), impl_req.vendor_ie, &impl_req.vendor_ie_len);
      }
    } else {
      impl_req.rsne_len = 0;
      impl_req.vendor_ie_len = 0;
    }
  }

  wlanif_impl_assoc_req(&wlanif_impl_, &impl_req);
}

void Device::AssociateResp(wlan_mlme::AssociateResponse resp) {
  wlanif_assoc_resp_t impl_resp = {};

  // peer_sta_address
  std::memcpy(impl_resp.peer_sta_address, resp.peer_sta_address.data(), ETH_ALEN);

  // result_code
  impl_resp.result_code = ConvertAssocResultCode(resp.result_code);

  // association_id
  impl_resp.association_id = resp.association_id;

  wlanif_impl_assoc_resp(&wlanif_impl_, &impl_resp);
}

void Device::DisassociateReq(wlan_mlme::DisassociateRequest req) {
  SetEthernetStatusUnlocked(false);

  wlanif_disassoc_req_t impl_req = {};

  // peer_sta_address
  std::memcpy(impl_req.peer_sta_address, req.peer_sta_address.data(), ETH_ALEN);

  // reason_code
  impl_req.reason_code = req.reason_code;

  wlanif_impl_disassoc_req(&wlanif_impl_, &impl_req);
}

void Device::ResetReq(wlan_mlme::ResetRequest req) {
  SetEthernetStatusUnlocked(false);

  wlanif_reset_req_t impl_req = {};

  // sta_address
  std::memcpy(impl_req.sta_address, req.sta_address.data(), ETH_ALEN);

  // set_default_mib
  impl_req.set_default_mib = req.set_default_mib;

  wlanif_impl_reset_req(&wlanif_impl_, &impl_req);
}

void Device::StartReq(wlan_mlme::StartRequest req) {
  SetEthernetStatusUnlocked(true);

  wlanif_start_req_t impl_req = {};

  // ssid
  CopySSID(req.ssid, &impl_req.ssid);

  // bss_type
  impl_req.bss_type = ConvertBSSType(req.bss_type);

  // beacon_period
  impl_req.beacon_period = req.beacon_period;

  // dtim_period
  impl_req.dtim_period = req.dtim_period;

  // channel
  impl_req.channel = req.channel;

  // rsne
  CopyRSNE(req.rsne.value_or(std::vector<uint8_t>{}), impl_req.rsne, &impl_req.rsne_len);

  wlanif_impl_start_req(&wlanif_impl_, &impl_req);
}

void Device::StopReq(wlan_mlme::StopRequest req) {
  SetEthernetStatusUnlocked(false);

  wlanif_stop_req_t impl_req = {};

  // ssid
  CopySSID(req.ssid, &impl_req.ssid);

  wlanif_impl_stop_req(&wlanif_impl_, &impl_req);
}

void Device::SetKeysReq(wlan_mlme::SetKeysRequest req) {
  wlanif_set_keys_req_t impl_req = {};

  // keylist
  size_t num_keys = req.keylist.size();
  if (num_keys > WLAN_MAX_KEYLIST_SIZE) {
    warnf("wlanif: truncating key list from %zu to %d members\n", num_keys, WLAN_MAX_KEYLIST_SIZE);
    impl_req.num_keys = WLAN_MAX_KEYLIST_SIZE;
  } else {
    impl_req.num_keys = num_keys;
  }
  for (size_t desc_ndx = 0; desc_ndx < num_keys; desc_ndx++) {
    ConvertSetKeyDescriptor(&impl_req.keylist[desc_ndx], req.keylist[desc_ndx]);
  }

  wlanif_impl_set_keys_req(&wlanif_impl_, &impl_req);
}

void Device::DeleteKeysReq(wlan_mlme::DeleteKeysRequest req) {
  wlanif_del_keys_req_t impl_req = {};

  // keylist
  size_t num_keys = req.keylist.size();
  if (num_keys > WLAN_MAX_KEYLIST_SIZE) {
    warnf("wlanif: truncating key list from %zu to %d members\n", num_keys, WLAN_MAX_KEYLIST_SIZE);
    impl_req.num_keys = WLAN_MAX_KEYLIST_SIZE;
  } else {
    impl_req.num_keys = num_keys;
  }
  for (size_t desc_ndx = 0; desc_ndx < num_keys; desc_ndx++) {
    ConvertDeleteKeyDescriptor(&impl_req.keylist[desc_ndx], req.keylist[desc_ndx]);
  }

  wlanif_impl_del_keys_req(&wlanif_impl_, &impl_req);
}

void Device::EapolReq(wlan_mlme::EapolRequest req) {
  wlanif_eapol_req_t impl_req = {};

  // src_addr
  std::memcpy(impl_req.src_addr, req.src_addr.data(), ETH_ALEN);

  // dst_addr
  std::memcpy(impl_req.dst_addr, req.dst_addr.data(), ETH_ALEN);

  // data
  impl_req.data_count = req.data.size();
  impl_req.data_list = req.data.data();

  wlanif_impl_eapol_req(&wlanif_impl_, &impl_req);
}

void Device::QueryDeviceInfo(QueryDeviceInfoCallback cb) {
  std::lock_guard<std::mutex> lock(lock_);

  if (!binding_.is_bound()) {
    return;
  }

  wlan_mlme::DeviceInfo fidl_resp;

  // mac_addr
  std::memcpy(fidl_resp.mac_addr.data(), query_info_.mac_addr, ETH_ALEN);

  // role
  fidl_resp.role = ConvertMacRole(query_info_.role);

  // bands
  fidl_resp.bands.resize(query_info_.num_bands);
  for (size_t ndx = 0; ndx < query_info_.num_bands; ndx++) {
    ConvertBandCapabilities(&fidl_resp.bands[ndx], query_info_.bands[ndx]);
  }

  // driver features flag
  fidl_resp.driver_features.resize(0);
  if (query_info_.driver_features & WLAN_INFO_DRIVER_FEATURE_SCAN_OFFLOAD) {
    fidl_resp.driver_features.push_back(wlan_common::DriverFeature::SCAN_OFFLOAD);
  }
  if (query_info_.driver_features & WLAN_INFO_DRIVER_FEATURE_RATE_SELECTION) {
    fidl_resp.driver_features.push_back(wlan_common::DriverFeature::RATE_SELECTION);
  }
  if (query_info_.driver_features & WLAN_INFO_DRIVER_FEATURE_SYNTH) {
    fidl_resp.driver_features.push_back(wlan_common::DriverFeature::SYNTH);
  }
  if (query_info_.driver_features & WLAN_INFO_DRIVER_FEATURE_TX_STATUS_REPORT) {
    fidl_resp.driver_features.push_back(wlan_common::DriverFeature::TX_STATUS_REPORT);
  }
  if (query_info_.driver_features & WLAN_INFO_DRIVER_FEATURE_DFS) {
    fidl_resp.driver_features.push_back(wlan_common::DriverFeature::DFS);
  }
  if (query_info_.driver_features & WLAN_INFO_DRIVER_FEATURE_PROBE_RESP_OFFLOAD) {
    fidl_resp.driver_features.push_back(wlan_common::DriverFeature::PROBE_RESP_OFFLOAD);
  }

  cb(std::move(fidl_resp));
}

void Device::StatsQueryReq() {
  if (wlanif_impl_.ops->stats_query_req != nullptr) {
    wlanif_impl_stats_query_req(&wlanif_impl_);
  }
}

void Device::ListMinstrelPeers(ListMinstrelPeersCallback cb) {
  errorf("Minstrel peer list not available: FullMAC driver not supported.\n");
  ZX_DEBUG_ASSERT(false);

  std::lock_guard<std::mutex> lock(lock_);
  if (!binding_.is_bound()) {
    return;
  }

  cb(wlan_mlme::MinstrelListResponse{});
}

void Device::GetMinstrelStats(wlan_mlme::MinstrelStatsRequest req, GetMinstrelStatsCallback cb) {
  errorf("Minstrel stats not available: FullMAC driver not supported.\n");
  ZX_DEBUG_ASSERT(false);

  std::lock_guard<std::mutex> lock(lock_);
  if (!binding_.is_bound()) {
    return;
  }

  cb(wlan_mlme::MinstrelStatsResponse{});
}

void Device::SendMpOpenAction(wlan_mlme::MeshPeeringOpenAction req) {
  errorf("SendMpConfirmAction is not implemented\n");
}

void Device::SendMpConfirmAction(wlan_mlme::MeshPeeringConfirmAction req) {
  errorf("SendMpConfirmAction is not implemented\n");
}

void Device::MeshPeeringEstablished(wlan_mlme::MeshPeeringParams params) {
  errorf("MeshPeeringEstablished is not implemented\n");
}

void Device::GetMeshPathTableReq(::fuchsia::wlan::mlme::GetMeshPathTableRequest req,
                                 GetMeshPathTableReqCallback cb) {
  errorf("GetMeshPathTable is not implemented\n");
}

void Device::SetControlledPort(wlan_mlme::SetControlledPortRequest req) {
  switch (req.state) {
    case wlan_mlme::ControlledPortState::OPEN:
      SetEthernetStatusUnlocked(true);
      break;
    case wlan_mlme::ControlledPortState::CLOSED:
      SetEthernetStatusUnlocked(false);
      break;
  }
}

void Device::StartCaptureFrames(::fuchsia::wlan::mlme::StartCaptureFramesRequest req,
                                StartCaptureFramesCallback cb) {
  wlanif_start_capture_frames_req_t impl_req = {};
  impl_req.mgmt_frame_flags = ConvertMgmtCaptureFlags(req.mgmt_frame_flags);

  wlanif_start_capture_frames_resp_t impl_resp = {};

  // forward request to driver
  wlanif_impl_start_capture_frames(&wlanif_impl_, &impl_req, &impl_resp);

  wlan_mlme::StartCaptureFramesResponse resp;
  resp.status = impl_resp.status;
  resp.supported_mgmt_frames = ConvertMgmtCaptureFlags(impl_resp.supported_mgmt_frames);
  cb(resp);
}

void Device::StopCaptureFrames() { wlanif_impl_stop_capture_frames(&wlanif_impl_); }

void Device::SaeHandshakeResp(::fuchsia::wlan::mlme::SaeHandshakeResponse resp) {
  // TODO(fxb/40006): Implement.
}

void Device::SaeFrameTx(::fuchsia::wlan::mlme::SaeFrame frame) {
  // TODO(fxb/40006): Implement.
}

void Device::OnScanResult(const wlanif_scan_result_t* result) {
  std::lock_guard<std::mutex> lock(lock_);
  if (!binding_.is_bound()) {
    return;
  }

  wlan_mlme::ScanResult fidl_result;

  // txn_id
  fidl_result.txn_id = result->txn_id;

  // bss
  ConvertBSSDescription(&fidl_result.bss, result->bss);

  binding_.events().OnScanResult(std::move(fidl_result));
}

void Device::OnScanEnd(const wlanif_scan_end_t* end) {
  std::lock_guard<std::mutex> lock(lock_);
  if (!binding_.is_bound()) {
    return;
  }

  wlan_mlme::ScanEnd fidl_end;

  // txn_id
  fidl_end.txn_id = end->txn_id;

  // code
  fidl_end.code = ConvertScanResultCode(end->code);

  binding_.events().OnScanEnd(std::move(fidl_end));
}

void Device::JoinConf(const wlanif_join_confirm_t* resp) {
  std::lock_guard<std::mutex> lock(lock_);

  SetEthernetStatusLocked(false);

  if (!binding_.is_bound()) {
    return;
  }

  wlan_mlme::JoinConfirm fidl_resp;

  // result_code
  fidl_resp.result_code = ConvertJoinResultCode(resp->result_code);

  binding_.events().JoinConf(std::move(fidl_resp));
}

void Device::AuthenticateConf(const wlanif_auth_confirm_t* resp) {
  std::lock_guard<std::mutex> lock(lock_);

  SetEthernetStatusLocked(false);

  if (!binding_.is_bound()) {
    return;
  }

  wlan_mlme::AuthenticateConfirm fidl_resp;

  // peer_sta_address
  std::memcpy(fidl_resp.peer_sta_address.data(), resp->peer_sta_address, ETH_ALEN);

  // auth_type
  fidl_resp.auth_type = ConvertAuthType(resp->auth_type);

  // result_code
  fidl_resp.result_code = ConvertAuthResultCode(resp->result_code);

  binding_.events().AuthenticateConf(std::move(fidl_resp));
}

void Device::AuthenticateInd(const wlanif_auth_ind_t* ind) {
  std::lock_guard<std::mutex> lock(lock_);

  if (!binding_.is_bound()) {
    return;
  }

  wlan_mlme::AuthenticateIndication fidl_ind;

  // peer_sta_address
  std::memcpy(fidl_ind.peer_sta_address.data(), ind->peer_sta_address, ETH_ALEN);

  // auth_type
  fidl_ind.auth_type = ConvertAuthType(ind->auth_type);

  binding_.events().AuthenticateInd(std::move(fidl_ind));
}

void Device::DeauthenticateConf(const wlanif_deauth_confirm_t* resp) {
  std::lock_guard<std::mutex> lock(lock_);

  if (query_info_.role == WLAN_INFO_MAC_ROLE_CLIENT) {
    SetEthernetStatusLocked(false);
  }

  if (!binding_.is_bound()) {
    return;
  }

  wlan_mlme::DeauthenticateConfirm fidl_resp;

  // peer_sta_address
  std::memcpy(fidl_resp.peer_sta_address.data(), resp->peer_sta_address, ETH_ALEN);

  binding_.events().DeauthenticateConf(std::move(fidl_resp));
}

void Device::DeauthenticateInd(const wlanif_deauth_indication_t* ind) {
  std::lock_guard<std::mutex> lock(lock_);

  if (query_info_.role == WLAN_INFO_MAC_ROLE_CLIENT) {
    SetEthernetStatusLocked(false);
  }

  if (!binding_.is_bound()) {
    return;
  }

  wlan_mlme::DeauthenticateIndication fidl_ind;

  // peer_sta_address
  std::memcpy(fidl_ind.peer_sta_address.data(), ind->peer_sta_address, ETH_ALEN);

  // reason_code
  fidl_ind.reason_code = ConvertDeauthReasonCode(ind->reason_code);

  // locally_initiated
  fidl_ind.locally_initiated = ind->locally_initiated;

  binding_.events().DeauthenticateInd(std::move(fidl_ind));
}

void Device::AssociateConf(const wlanif_assoc_confirm_t* resp) {
  std::lock_guard<std::mutex> lock(lock_);

  // For unprotected network, set data state to online immediately. For protected network, do
  // nothing. Later on upper layer would send message to open controlled port.
  if (resp->result_code == WLAN_ASSOC_RESULT_SUCCESS && !protected_bss_) {
    SetEthernetStatusLocked(true);
  }

  if (!binding_.is_bound()) {
    return;
  }

  wlan_mlme::AssociateConfirm fidl_resp;

  // result_code
  fidl_resp.result_code = ConvertAssocResultCode(resp->result_code);

  // association_id
  fidl_resp.association_id = resp->association_id;

  if (resp->wmm_param_present) {
    // Sanity check that the param length in banjo and FIDL are the same
    static_assert(WLAN_WMM_PARAM_LEN == wlan_mlme::WMM_PARAM_LEN);
    auto wmm_param = wlan_mlme::WmmParameter::New();
    memcpy(wmm_param->bytes.data(), resp->wmm_param, WLAN_WMM_PARAM_LEN);
    fidl_resp.wmm_param = std::move(wmm_param);
  }

  binding_.events().AssociateConf(std::move(fidl_resp));
}

void Device::AssociateInd(const wlanif_assoc_ind_t* ind) {
  std::lock_guard<std::mutex> lock(lock_);
  if (!binding_.is_bound()) {
    return;
  }

  wlan_mlme::AssociateIndication fidl_ind;

  ConvertAssocInd(&fidl_ind, *ind);
  binding_.events().AssociateInd(std::move(fidl_ind));
}

void Device::DisassociateConf(const wlanif_disassoc_confirm_t* resp) {
  std::lock_guard<std::mutex> lock(lock_);

  if (query_info_.role == WLAN_INFO_MAC_ROLE_CLIENT) {
    SetEthernetStatusLocked(false);
  }

  if (!binding_.is_bound()) {
    return;
  }

  wlan_mlme::DisassociateConfirm fidl_resp;

  // status
  fidl_resp.status = resp->status;

  binding_.events().DisassociateConf(std::move(fidl_resp));
}

void Device::DisassociateInd(const wlanif_disassoc_indication_t* ind) {
  std::lock_guard<std::mutex> lock(lock_);

  if (query_info_.role == WLAN_INFO_MAC_ROLE_CLIENT) {
    SetEthernetStatusLocked(false);
  }

  if (!binding_.is_bound()) {
    return;
  }

  wlan_mlme::DisassociateIndication fidl_ind;

  // peer_sta_address
  std::memcpy(fidl_ind.peer_sta_address.data(), ind->peer_sta_address, ETH_ALEN);

  // reason_code
  fidl_ind.reason_code = ind->reason_code;

  // locally_initiated
  fidl_ind.locally_initiated = ind->locally_initiated;

  binding_.events().DisassociateInd(std::move(fidl_ind));
}

void Device::StartConf(const wlanif_start_confirm_t* resp) {
  std::lock_guard<std::mutex> lock(lock_);

  if (resp->result_code == WLAN_START_RESULT_SUCCESS) {
    SetEthernetStatusLocked(true);
  }

  if (!binding_.is_bound()) {
    return;
  }

  wlan_mlme::StartConfirm fidl_resp;

  // result_code
  fidl_resp.result_code = ConvertStartResultCode(resp->result_code);

  binding_.events().StartConf(std::move(fidl_resp));
}

void Device::StopConf(const wlanif_stop_confirm_t* resp) {
  std::lock_guard<std::mutex> lock(lock_);

  if (resp->result_code == WLAN_STOP_RESULT_SUCCESS) {
    SetEthernetStatusLocked(false);
  }

  if (!binding_.is_bound()) {
    return;
  }

  wlan_mlme::StopConfirm fidl_resp;
  fidl_resp.result_code = ConvertStopResultCode(resp->result_code);

  binding_.events().StopConf(fidl_resp);
}

void Device::EapolConf(const wlanif_eapol_confirm_t* resp) {
  std::lock_guard<std::mutex> lock(lock_);
  if (!binding_.is_bound()) {
    return;
  }

  wlan_mlme::EapolConfirm fidl_resp;

  // result_code
  fidl_resp.result_code = ConvertEapolResultCode(resp->result_code);

  binding_.events().EapolConf(std::move(fidl_resp));
}

void Device::OnChannelSwitched(const wlanif_channel_switch_info_t* info) {
  std::lock_guard<std::mutex> lock(lock_);
  if (!binding_.is_bound()) {
    return;
  }

  wlan_mlme::ChannelSwitchInfo fidl_info;
  fidl_info.new_channel = info->new_channel;

  binding_.events().OnChannelSwitched(fidl_info);
}

void Device::SignalReport(const wlanif_signal_report_indication_t* ind) {
  std::lock_guard<std::mutex> lock(lock_);
  if (!binding_.is_bound()) {
    return;
  }

  wlan_mlme::SignalReportIndication fidl_ind{
      .rssi_dbm = ind->rssi_dbm,
      .snr_db = ind->snr_db,
  };

  binding_.events().SignalReport(std::move(fidl_ind));
}

void Device::EapolInd(const wlanif_eapol_indication_t* ind) {
  std::lock_guard<std::mutex> lock(lock_);
  if (!binding_.is_bound()) {
    return;
  }

  wlan_mlme::EapolIndication fidl_ind;

  // src_addr
  std::memcpy(fidl_ind.src_addr.data(), ind->src_addr, ETH_ALEN);

  // dst_addr
  std::memcpy(fidl_ind.dst_addr.data(), ind->dst_addr, ETH_ALEN);

  // data
  fidl_ind.data.resize(ind->data_count);
  fidl_ind.data.assign(ind->data_list, ind->data_list + ind->data_count);

  binding_.events().EapolInd(std::move(fidl_ind));
}

void Device::StatsQueryResp(const wlanif_stats_query_response_t* resp) {
  std::lock_guard<std::mutex> lock(lock_);
  if (!binding_.is_bound()) {
    return;
  }

  wlan_mlme::StatsQueryResponse fidl_resp;
  ConvertIfaceStats(&fidl_resp.stats, resp->stats);
  binding_.events().StatsQueryResp(std::move(fidl_resp));
}

void Device::RelayCapturedFrame(const wlanif_captured_frame_result* result) {
  std::lock_guard<std::mutex> lock(lock_);
  if (!binding_.is_bound()) {
    return;
  }

  wlan_mlme::CapturedFrameResult fidl_result;
  fidl_result.frame.resize(result->data_count);
  fidl_result.frame.assign(result->data_list, result->data_list + result->data_count);

  binding_.events().RelayCapturedFrame(std::move(fidl_result));
}

zx_status_t Device::EthStart(const ethernet_ifc_protocol_t* ifc) {
  std::lock_guard<std::mutex> lock(lock_);
  ethernet_ifc_ = *ifc;
  eth_started_ = true;
  if (eth_online_) {
    ethernet_ifc_status(&ethernet_ifc_, ETHERNET_STATUS_ONLINE);
  }
  // TODO(fxbug.dev/51009): Inform SME that ethernet has started.
  return ZX_OK;
}

void Device::EthStop() {
  std::lock_guard<std::mutex> lock(lock_);
  eth_started_ = false;
  std::memset(&ethernet_ifc_, 0, sizeof(ethernet_ifc_));
}

zx_status_t Device::EthQuery(uint32_t options, ethernet_info_t* info) {
  std::lock_guard<std::mutex> lock(lock_);

  std::memset(info, 0, sizeof(*info));

  // features
  info->features = ETHERNET_FEATURE_WLAN;
  if (query_info_.features & WLANIF_FEATURE_DMA) {
    info->features |= ETHERNET_FEATURE_DMA;
  }
  if (query_info_.features & WLANIF_FEATURE_SYNTH) {
    info->features |= ETHERNET_FEATURE_SYNTH;
  }

  // mtu
  info->mtu = 1500;
  info->netbuf_size = sizeof(ethernet_netbuf_t);

  // mac
  std::memcpy(info->mac, query_info_.mac_addr, ETH_ALEN);

  return ZX_OK;
}

void Device::EthQueueTx(uint32_t options, ethernet_netbuf_t* netbuf,
                        ethernet_impl_queue_tx_callback completion_cb, void* cookie) {
  if (wlanif_impl_.ops->data_queue_tx != nullptr) {
    wlanif_impl_data_queue_tx(&wlanif_impl_, options, netbuf, completion_cb, cookie);
  } else {
    completion_cb(cookie, ZX_ERR_NOT_SUPPORTED, netbuf);
  }
}

zx_status_t Device::EthSetParam(uint32_t param, int32_t value, const void* data, size_t data_size) {
  zx_status_t status = ZX_ERR_NOT_SUPPORTED;

  switch (param) {
    case ETHERNET_SETPARAM_PROMISC:
      // See WLAN-259: In short, the bridge mode doesn't require WLAN promiscuous mode enabled.
      //               So we give a warning and return OK here to continue the bridging.
      // TODO(WLAN-491): To implement the real promiscuous mode.
      if (value == 1) {  // Only warn when enabling.
        warnf("wlanif: WLAN promiscuous not supported yet. see WLAN-491\n");
      }
      status = ZX_OK;
      break;
    case ETHERNET_SETPARAM_MULTICAST_PROMISC:
      if (wlanif_impl_.ops->set_multicast_promisc != nullptr) {
        return wlanif_impl_set_multicast_promisc(&wlanif_impl_, !!value);
      } else {
        return ZX_ERR_NOT_SUPPORTED;
      }
      break;
  }

  return status;
}

void Device::SetEthernetStatusLocked(bool online) {
  // TODO(fxbug.dev/51009): Let SME handle these changes.
  if (online != eth_online_) {
    eth_online_ = online;
    if (eth_started_) {
      ethernet_ifc_status(&ethernet_ifc_, online ? ETHERNET_STATUS_ONLINE : 0);
    }
  }
}

void Device::SetEthernetStatusUnlocked(bool online) {
  std::lock_guard<std::mutex> lock(lock_);
  SetEthernetStatusLocked(online);
}

void Device::EthRecv(const void* data, size_t length, uint32_t flags) {
  std::lock_guard<std::mutex> lock(lock_);
  if (eth_started_) {
    ethernet_ifc_recv(&ethernet_ifc_, data, length, flags);
  }
}

}  // namespace wlanif
