// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "test_bss.h"

#include <fuchsia/wlan/mlme/cpp/fidl.h>

#include <memory>

#include <gtest/gtest.h>
#include <wlan/common/buffer_writer.h>
#include <wlan/common/channel.h>
#include <wlan/common/write_element.h>
#include <wlan/mlme/mac_frame.h>
#include <wlan/mlme/packet.h>
#include <wlan/mlme/ps_cfg.h>
#include <wlan/mlme/rates_elements.h>
#include <wlan/mlme/service.h>

#include "mock_device.h"

namespace wlan {

namespace wlan_common = ::fuchsia::wlan::common;
namespace wlan_mlme = ::fuchsia::wlan::mlme;

void WriteTim(BufferWriter* w, const PsCfg& ps_cfg) {
  size_t bitmap_len = ps_cfg.GetTim()->BitmapLen();
  uint8_t bitmap_offset = ps_cfg.GetTim()->BitmapOffset();

  TimHeader hdr;
  hdr.dtim_count = ps_cfg.dtim_count();
  hdr.dtim_period = ps_cfg.dtim_period();
  ZX_DEBUG_ASSERT(hdr.dtim_count != hdr.dtim_period);
  if (hdr.dtim_count == hdr.dtim_period) {
    warnf("illegal DTIM state");
  }

  hdr.bmp_ctrl.set_offset(bitmap_offset);
  if (ps_cfg.IsDtim()) {
    hdr.bmp_ctrl.set_group_traffic_ind(ps_cfg.GetTim()->HasGroupTraffic());
  }
  common::WriteTim(w, hdr, {ps_cfg.GetTim()->BitmapData(), bitmap_len});
}

void WriteCountry(BufferWriter* w, const wlan_channel_t chan) {
  const Country kCountry = {{'U', 'S', ' '}};

  std::vector<SubbandTriplet> subbands;

  // TODO(porce): Read from the AP's regulatory domain
  if (wlan::common::Is2Ghz(chan)) {
    subbands.push_back({1, 11, 36});
  } else {
    subbands.push_back({36, 4, 36});
    subbands.push_back({52, 4, 30});
    subbands.push_back({100, 12, 30});
    subbands.push_back({149, 5, 36});
  }

  common::WriteCountry(w, kCountry, subbands);
}

wlan_mlme::BSSDescription CreateBssDescription(bool rsne, wlan_channel_t chan) {
  common::MacAddr bssid(kBssid1);

  wlan_mlme::BSSDescription bss_desc;
  std::memcpy(bss_desc.bssid.data(), bssid.byte, common::kMacAddrLen);
  std::vector<uint8_t> ssid(kSsid, kSsid + sizeof(kSsid));
  bss_desc.ssid = std::move(ssid);
  bss_desc.bss_type = wlan_mlme::BSSTypes::INFRASTRUCTURE;
  bss_desc.beacon_period = kBeaconPeriodTu;
  bss_desc.dtim_period = kDtimPeriodTu;
  bss_desc.timestamp = 0;
  bss_desc.local_time = 0;
  bss_desc.rates.resize(0);

  CapabilityInfo cap{};
  cap.set_ess(true);
  cap.set_short_preamble(true);
  bss_desc.cap = cap.val();

  if (rsne) {
    bss_desc.rsne.emplace(std::vector<uint8_t>(kRsne, kRsne + sizeof(kRsne)));
  } else {
    bss_desc.rsne.reset();
  }
  bss_desc.rcpi_dbmh = 0;
  bss_desc.rsni_dbh = 0;

  bss_desc.ht_cap.reset();
  bss_desc.ht_op.reset();

  bss_desc.vht_cap.reset();
  bss_desc.vht_op.reset();

  bss_desc.chan.cbw = static_cast<wlan_common::CBW>(chan.cbw);
  bss_desc.chan.primary = chan.primary;

  bss_desc.rssi_dbm = -35;

  return bss_desc;
}

MlmeMsg<wlan_mlme::ScanRequest> CreateScanRequest(uint32_t max_channel_time) {
  auto req = wlan_mlme::ScanRequest::New();
  req->txn_id = 0;
  req->bss_type = wlan_mlme::BSSTypes::ANY_BSS;
  std::memcpy(req->bssid.data(), kBroadcastBssid, sizeof(kBroadcastBssid));
  req->ssid = {0};
  req->scan_type = wlan_mlme::ScanTypes::PASSIVE;
  req->channel_list.emplace({11});
  req->max_channel_time = max_channel_time;

  return {std::move(*req), fuchsia::wlan::mlme::internal::kMLME_StartScan_Ordinal};
}

MlmeMsg<wlan_mlme::StartRequest> CreateStartRequest(bool protected_ap) {
  auto req = wlan_mlme::StartRequest::New();
  std::vector<uint8_t> ssid(kSsid, kSsid + sizeof(kSsid));
  req->ssid = std::move(ssid);
  req->bss_type = wlan_mlme::BSSTypes::INFRASTRUCTURE;
  req->beacon_period = kBeaconPeriodTu;
  req->dtim_period = kDtimPeriodTu;
  req->channel = kBssChannel.primary;
  req->rates = {0x82, 0x84, 0x8b, 0x96, 0x0c, 0x12, 0x18, 0x24, 0x30, 0x48, 0x60, 0x6c};
  req->mesh_id.resize(0);
  req->phy = wlan_common::PHY::ERP;
  if (protected_ap) {
    req->rsne.emplace(std::vector<uint8_t>(kRsne, kRsne + sizeof(kRsne)));
  }

  return {std::move(*req), fuchsia::wlan::mlme::internal::kMLME_StartReq_Ordinal};
}

MlmeMsg<wlan_mlme::StopRequest> CreateStopRequest() {
  auto req = wlan_mlme::StopRequest::New();
  req->ssid = std::vector<uint8_t>(kSsid, kSsid + sizeof(kSsid));
  return {std::move(*req), fuchsia::wlan::mlme::internal::kMLME_StopReq_Ordinal};
}

MlmeMsg<wlan_mlme::JoinRequest> CreateJoinRequest(bool rsn) {
  auto req = wlan_mlme::JoinRequest::New();
  req->join_failure_timeout = kJoinTimeout;
  req->nav_sync_delay = 20;
  req->op_rates = {12, 24, 48};
  req->phy = wlan::common::ToFidl(kBssPhy);
  req->cbw = wlan::common::ToFidl(kBssChannel).cbw;
  req->selected_bss = CreateBssDescription(rsn);
  req->selected_bss.rates = {12, 24, 48};

  return {std::move(*req), fuchsia::wlan::mlme::internal::kMLME_JoinReq_Ordinal};
}

MlmeMsg<wlan_mlme::AuthenticateRequest> CreateAuthRequest() {
  common::MacAddr bssid(kBssid1);

  auto req = wlan_mlme::AuthenticateRequest::New();
  std::memcpy(req->peer_sta_address.data(), bssid.byte, common::kMacAddrLen);
  req->auth_failure_timeout = kAuthTimeout;
  req->auth_type = wlan_mlme::AuthenticationTypes::OPEN_SYSTEM;

  return {std::move(*req), fuchsia::wlan::mlme::internal::kMLME_AuthenticateReq_Ordinal};
}

MlmeMsg<wlan_mlme::DeauthenticateRequest> CreateDeauthRequest(common::MacAddr peer_addr,
                                                              wlan_mlme::ReasonCode reason_code) {
  auto req = wlan_mlme::DeauthenticateRequest::New();
  std::memcpy(req->peer_sta_address.data(), peer_addr.byte, common::kMacAddrLen);
  req->reason_code = reason_code;
  return {std::move(*req), fuchsia::wlan::mlme::internal::kMLME_DeauthenticateReq_Ordinal};
}

MlmeMsg<wlan_mlme::AuthenticateResponse> CreateAuthResponse(
    common::MacAddr client_addr, wlan_mlme::AuthenticateResultCodes result_code) {
  auto resp = wlan_mlme::AuthenticateResponse::New();
  std::memcpy(resp->peer_sta_address.data(), client_addr.byte, common::kMacAddrLen);
  resp->result_code = result_code;

  return {std::move(*resp), fuchsia::wlan::mlme::internal::kMLME_AuthenticateResp_Ordinal};
}

MlmeMsg<wlan_mlme::AssociateRequest> CreateAssocRequest(bool rsne) {
  common::MacAddr bssid(kBssid1);

  auto req = wlan_mlme::AssociateRequest::New();
  std::memcpy(req->peer_sta_address.data(), bssid.byte, common::kMacAddrLen);
  req->rates = {std::cbegin(kRates), std::cend(kRates)};
  if (rsne) {
    req->rsne.emplace(std::vector<uint8_t>(kRsne, kRsne + sizeof(kRsne)));
  } else {
    req->rsne.reset();
  }

  return {std::move(*req), fuchsia::wlan::mlme::internal::kMLME_AssociateReq_Ordinal};
}

MlmeMsg<wlan_mlme::AssociateResponse> CreateAssocResponse(
    common::MacAddr client_addr, wlan_mlme::AssociateResultCodes result_code, uint16_t aid) {
  auto resp = wlan_mlme::AssociateResponse::New();
  std::memcpy(resp->peer_sta_address.data(), client_addr.byte, common::kMacAddrLen);
  resp->result_code = result_code;
  resp->association_id = aid;
  resp->rates = {0x82, 0x84, 0x8b, 0x96, 0x0c, 0x12, 0x18, 0x24, 0x30, 0x48, 0x60, 0x6c};

  return {std::move(*resp), fuchsia::wlan::mlme::internal::kMLME_AssociateResp_Ordinal};
}

MlmeMsg<wlan_mlme::NegotiatedCapabilities> CreateFinalizeAssociationRequest(
    const wlan_assoc_ctx& ac, wlan_channel_t chan) {
  auto cap = wlan_mlme::NegotiatedCapabilities::New();
  cap->channel.primary = chan.primary;
  cap->channel.cbw = static_cast<wlan_common::CBW>(chan.cbw);
  cap->channel.secondary80 = chan.secondary80;

  cap->cap_info = ac.cap_info;
  cap->rates.assign(ac.rates, ac.rates + ac.rates_cnt);
  if (ac.has_ht_cap) {
    cap->ht_cap = wlan_mlme::HtCapabilities::New();
    static_assert(sizeof(cap->ht_cap->bytes) == sizeof(ac.ht_cap));
    memcpy(cap->ht_cap->bytes.data(), &ac.ht_cap, sizeof(ac.ht_cap));
  }

  if (ac.has_vht_cap) {
    cap->vht_cap = wlan_mlme::VhtCapabilities::New();
    static_assert(sizeof(cap->vht_cap->bytes) == sizeof(ac.vht_cap));
    memcpy(cap->vht_cap->bytes.data(), &ac.vht_cap, sizeof(ac.vht_cap));
  }

  return {std::move(*cap), fuchsia::wlan::mlme::internal::kMLME_FinalizeAssociationReq_Ordinal};
}

MlmeMsg<wlan_mlme::EapolRequest> CreateEapolRequest(common::MacAddr src_addr,
                                                    common::MacAddr dst_addr) {
  auto req = wlan_mlme::EapolRequest::New();
  std::memcpy(req->src_addr.data(), src_addr.byte, common::kMacAddrLen);
  std::memcpy(req->dst_addr.data(), dst_addr.byte, common::kMacAddrLen);
  std::vector<uint8_t> eapol_pdu(kEapolPdu, kEapolPdu + sizeof(kEapolPdu));
  req->data = std::move(eapol_pdu);

  return {std::move(*req), fuchsia::wlan::mlme::internal::kMLME_EapolReq_Ordinal};
}

MlmeMsg<wlan_mlme::SetKeysRequest> CreateSetKeysRequest(common::MacAddr addr,
                                                        std::vector<uint8_t> key_data,
                                                        wlan_mlme::KeyType key_type) {
  wlan_mlme::SetKeyDescriptor key;
  key.key = key_data;
  key.key_id = 1;
  key.key_type = key_type;
  std::memcpy(key.address.data(), addr.byte, sizeof(addr));
  std::memcpy(key.cipher_suite_oui.data(), kCipherOui, sizeof(kCipherOui));
  key.cipher_suite_type = kCipherSuiteType;

  std::vector<wlan_mlme::SetKeyDescriptor> keylist;
  keylist.emplace_back(std::move(key));
  auto req = wlan_mlme::SetKeysRequest::New();
  req->keylist = std::move(keylist);

  return {std::move(*req), fuchsia::wlan::mlme::internal::kMLME_SetKeysReq_Ordinal};
}

MlmeMsg<wlan_mlme::SetControlledPortRequest> CreateSetCtrlPortRequest(
    common::MacAddr peer_addr, wlan_mlme::ControlledPortState state) {
  auto req = wlan_mlme::SetControlledPortRequest::New();
  std::memcpy(req->peer_sta_address.data(), peer_addr.byte, sizeof(peer_addr));
  req->state = state;

  return {std::move(*req), fuchsia::wlan::mlme::internal::kMLME_SetControlledPort_Ordinal};
}

std::unique_ptr<Packet> CreateBeaconFrame(common::MacAddr bssid) {
  constexpr size_t ie_len = 256;
  constexpr size_t max_frame_len = MgmtFrameHeader::max_len() + Beacon::max_len() + ie_len;
  auto packet = GetWlanPacket(max_frame_len);
  ZX_DEBUG_ASSERT(packet != nullptr);

  BufferWriter w(*packet);
  auto mgmt_hdr = w.Write<MgmtFrameHeader>();
  mgmt_hdr->fc.set_type(FrameType::kManagement);
  mgmt_hdr->fc.set_subtype(ManagementSubtype::kBeacon);
  mgmt_hdr->addr1 = common::kBcastMac;
  mgmt_hdr->addr2 = bssid;
  mgmt_hdr->addr3 = bssid;

  auto bcn = w.Write<Beacon>();
  bcn->beacon_interval = kBeaconPeriodTu;
  bcn->timestamp = 0;
  bcn->cap.set_ess(1);
  bcn->cap.set_short_preamble(1);

  BufferWriter elem_w(w.RemainingBuffer());
  common::WriteSsid(&elem_w, kSsid);
  RatesWriter rates_writer{kSupportedRates};
  rates_writer.WriteSupportedRates(&elem_w);
  common::WriteDsssParamSet(&elem_w, kBssChannel.primary);
  WriteCountry(&elem_w, kBssChannel);
  rates_writer.WriteExtendedSupportedRates(&elem_w);

  packet->set_len(w.WrittenBytes() + elem_w.WrittenBytes());

  wlan_rx_info_t rx_info{.rx_flags = 0, .chan = kBssChannel};
  packet->CopyCtrlFrom(rx_info);

  return packet;
}

std::unique_ptr<Packet> CreateProbeRequest() {
  common::MacAddr bssid(kBssid1);
  common::MacAddr client(kClientAddress);

  constexpr size_t ie_len = 256;
  constexpr size_t max_frame_len = MgmtFrameHeader::max_len() + ProbeRequest::max_len() + ie_len;
  auto packet = GetWlanPacket(max_frame_len);
  ZX_DEBUG_ASSERT(packet != nullptr);

  BufferWriter w(*packet);
  auto mgmt_hdr = w.Write<MgmtFrameHeader>();
  mgmt_hdr->fc.set_type(FrameType::kManagement);
  mgmt_hdr->fc.set_subtype(ManagementSubtype::kProbeRequest);
  mgmt_hdr->addr1 = bssid;
  mgmt_hdr->addr2 = client;
  mgmt_hdr->addr3 = bssid;

  w.Write<ProbeRequest>();
  BufferWriter elem_w(w.RemainingBuffer());
  common::WriteSsid(&elem_w, kSsid);

  RatesWriter rates_writer{kSupportedRates};
  rates_writer.WriteSupportedRates(&elem_w);
  rates_writer.WriteExtendedSupportedRates(&elem_w);
  common::WriteDsssParamSet(&elem_w, kBssChannel.primary);

  packet->set_len(w.WrittenBytes() + elem_w.WrittenBytes());

  wlan_rx_info_t rx_info{.rx_flags = 0, .chan = kBssChannel};
  packet->CopyCtrlFrom(rx_info);

  return packet;
}

std::unique_ptr<Packet> CreateAuthReqFrame(common::MacAddr client_addr) {
  common::MacAddr bssid(kBssid1);
  constexpr size_t max_frame_len = MgmtFrameHeader::max_len() + Authentication::max_len();
  auto packet = GetWlanPacket(max_frame_len);
  ZX_DEBUG_ASSERT(packet != nullptr);

  BufferWriter w(*packet);
  auto mgmt_hdr = w.Write<MgmtFrameHeader>();
  mgmt_hdr->fc.set_type(FrameType::kManagement);
  mgmt_hdr->fc.set_subtype(ManagementSubtype::kAuthentication);
  mgmt_hdr->addr1 = bssid;
  mgmt_hdr->addr2 = client_addr;
  mgmt_hdr->addr3 = bssid;

  auto auth = w.Write<Authentication>();
  auth->auth_algorithm_number = AuthAlgorithm::kOpenSystem;
  auth->auth_txn_seq_number = 1;
  auth->status_code = 0;  // Reserved: explicitly set to 0

  packet->set_len(w.WrittenBytes());

  wlan_rx_info_t rx_info{.rx_flags = 0, .chan = kBssChannel};
  packet->CopyCtrlFrom(rx_info);

  return packet;
}

std::unique_ptr<Packet> CreateAuthRespFrame(AuthAlgorithm auth_algo) {
  common::MacAddr bssid(kBssid1);
  common::MacAddr client(kClientAddress);

  constexpr size_t max_frame_len = MgmtFrameHeader::max_len() + Authentication::max_len();
  auto packet = GetWlanPacket(max_frame_len);
  ZX_DEBUG_ASSERT(packet != nullptr);

  BufferWriter w(*packet);
  auto mgmt_hdr = w.Write<MgmtFrameHeader>();
  mgmt_hdr->fc.set_type(FrameType::kManagement);
  mgmt_hdr->fc.set_subtype(ManagementSubtype::kAuthentication);
  mgmt_hdr->addr1 = client;
  mgmt_hdr->addr2 = bssid;
  mgmt_hdr->addr3 = bssid;

  auto auth = w.Write<Authentication>();
  auth->auth_algorithm_number = auth_algo;
  auth->auth_txn_seq_number = 2;
  auth->status_code = WLAN_STATUS_CODE_SUCCESS;

  packet->set_len(w.WrittenBytes());

  wlan_rx_info_t rx_info{.rx_flags = 0, .chan = kBssChannel};
  packet->CopyCtrlFrom(rx_info);

  return packet;
}

std::unique_ptr<Packet> CreateDeauthFrame(common::MacAddr client_addr) {
  common::MacAddr bssid(kBssid1);

  constexpr size_t max_frame_len = MgmtFrameHeader::max_len() + Deauthentication::max_len();
  auto packet = GetWlanPacket(max_frame_len);
  ZX_DEBUG_ASSERT(packet != nullptr);

  BufferWriter w(*packet);
  auto mgmt_hdr = w.Write<MgmtFrameHeader>();
  mgmt_hdr->fc.set_type(FrameType::kManagement);
  mgmt_hdr->fc.set_subtype(ManagementSubtype::kDeauthentication);
  mgmt_hdr->addr1 = bssid;
  mgmt_hdr->addr2 = client_addr;
  mgmt_hdr->addr3 = bssid;

  w.Write<Deauthentication>()->reason_code = WLAN_REASON_CODE_LEAVING_NETWORK_DEAUTH;

  packet->set_len(w.WrittenBytes());

  wlan_rx_info_t rx_info{.rx_flags = 0, .chan = kBssChannel};
  packet->CopyCtrlFrom(rx_info);

  return packet;
}

std::unique_ptr<Packet> CreateAssocReqFrame(common::MacAddr client_addr,
                                            fbl::Span<const uint8_t> ssid, bool rsn) {
  common::MacAddr bssid(kBssid1);

  // arbitrarily large reserved len; will shrink down later
  constexpr size_t ie_len = 1024;
  constexpr size_t max_frame_len =
      MgmtFrameHeader::max_len() + AssociationRequest::max_len() + ie_len;
  auto packet = GetWlanPacket(max_frame_len);
  ZX_DEBUG_ASSERT(packet != nullptr);

  BufferWriter w(*packet);
  auto mgmt_hdr = w.Write<MgmtFrameHeader>();
  mgmt_hdr->fc.set_type(FrameType::kManagement);
  mgmt_hdr->fc.set_subtype(ManagementSubtype::kAssociationRequest);
  mgmt_hdr->addr1 = bssid;
  mgmt_hdr->addr2 = client_addr;
  mgmt_hdr->addr3 = bssid;

  auto assoc = w.Write<AssociationRequest>();
  CapabilityInfo cap = {};
  cap.set_short_preamble(1);
  cap.set_ess(1);
  assoc->cap = cap;
  assoc->listen_interval = kListenInterval;

  BufferWriter elem_w(w.RemainingBuffer());
  if (!ssid.empty()) {
    common::WriteSsid(&w, ssid);
  }
  if (rsn) {
    w.Write(kRsne);
  }

  packet->set_len(w.WrittenBytes() + elem_w.WrittenBytes());

  wlan_rx_info_t rx_info{.rx_flags = 0, .chan = kBssChannel};
  packet->CopyCtrlFrom(rx_info);

  return packet;
}

std::unique_ptr<Packet> CreateAssocRespFrame(const wlan_assoc_ctx_t& ap_assoc_ctx) {
  common::MacAddr bssid(kBssid1);
  common::MacAddr client(kClientAddress);

  constexpr size_t reserved_ie_len = 256;
  constexpr size_t max_frame_len =
      MgmtFrameHeader::max_len() + AssociationResponse::max_len() + reserved_ie_len;
  auto packet = GetWlanPacket(max_frame_len);
  ZX_DEBUG_ASSERT(packet != nullptr);

  // TODO(fxbug.dev/29264): Implement a common frame builder
  BufferWriter w(*packet);
  auto mgmt_hdr = w.Write<MgmtFrameHeader>();
  mgmt_hdr->fc.set_type(FrameType::kManagement);
  mgmt_hdr->fc.set_subtype(ManagementSubtype::kAssociationResponse);
  mgmt_hdr->addr1 = client;
  mgmt_hdr->addr2 = bssid;
  mgmt_hdr->addr3 = bssid;

  auto assoc = w.Write<AssociationResponse>();
  assoc->aid = kAid;
  CapabilityInfo cap = {};
  cap.set_short_preamble(1);
  cap.set_ess(1);
  assoc->cap = cap;
  assoc->status_code = WLAN_STATUS_CODE_SUCCESS;

  BufferWriter elem_w(w.RemainingBuffer());
  if (ap_assoc_ctx.has_ht_cap) {
    common::WriteHtCapabilities(&elem_w, HtCapabilities::FromDdk(ap_assoc_ctx.ht_cap));
  }
  if (ap_assoc_ctx.has_ht_op) {
    common::WriteHtOperation(&elem_w, HtOperation::FromDdk(ap_assoc_ctx.ht_op));
  }
  if (ap_assoc_ctx.has_vht_cap) {
    common::WriteVhtCapabilities(&elem_w, VhtCapabilities::FromDdk(ap_assoc_ctx.vht_cap));
  }
  if (ap_assoc_ctx.has_vht_op) {
    common::WriteVhtOperation(&elem_w, VhtOperation::FromDdk(ap_assoc_ctx.vht_op));
  }

  packet->set_len(w.WrittenBytes() + elem_w.WrittenBytes());

  wlan_rx_info_t rx_info{.rx_flags = 0, .chan = kBssChannel};
  packet->CopyCtrlFrom(rx_info);

  return packet;
}

std::unique_ptr<Packet> CreateDisassocFrame(common::MacAddr client_addr) {
  common::MacAddr bssid(kBssid1);

  constexpr size_t max_frame_len = MgmtFrameHeader::max_len() + Disassociation::max_len();
  auto packet = GetWlanPacket(max_frame_len);
  ZX_DEBUG_ASSERT(packet != nullptr);

  BufferWriter w(*packet);
  auto mgmt_hdr = w.Write<MgmtFrameHeader>();
  mgmt_hdr->fc.set_type(FrameType::kManagement);
  mgmt_hdr->fc.set_subtype(ManagementSubtype::kDisassociation);
  mgmt_hdr->addr1 = bssid;
  mgmt_hdr->addr2 = client_addr;
  mgmt_hdr->addr3 = bssid;

  w.Write<Disassociation>()->reason_code = WLAN_REASON_CODE_LEAVING_NETWORK_DISASSOC;

  packet->set_len(w.WrittenBytes());

  wlan_rx_info_t rx_info{.rx_flags = 0, .chan = kBssChannel};
  packet->CopyCtrlFrom(rx_info);

  return packet;
}

std::unique_ptr<Packet> CreateDataFrame(fbl::Span<const uint8_t> payload) {
  common::MacAddr bssid(kBssid1);
  common::MacAddr client(kClientAddress);

  const size_t buf_len = DataFrameHeader::max_len() + LlcHeader::max_len() + payload.size_bytes();
  auto packet = GetWlanPacket(buf_len);
  ZX_DEBUG_ASSERT(packet != nullptr);

  BufferWriter w(*packet);
  auto data_hdr = w.Write<DataFrameHeader>();
  data_hdr->fc.set_type(FrameType::kData);
  data_hdr->fc.set_subtype(DataSubtype::kDataSubtype);
  data_hdr->fc.set_to_ds(0);
  data_hdr->fc.set_from_ds(1);
  data_hdr->addr1 = client;
  data_hdr->addr2 = bssid;
  data_hdr->addr3 = bssid;
  data_hdr->sc.set_val(42);

  auto llc_hdr = w.Write<LlcHeader>();
  llc_hdr->dsap = kLlcSnapExtension;
  llc_hdr->ssap = kLlcSnapExtension;
  llc_hdr->control = kLlcUnnumberedInformation;
  std::memcpy(llc_hdr->oui, kLlcOui, sizeof(llc_hdr->oui));
  llc_hdr->protocol_id_be = 42;
  w.Write(payload);

  packet->set_len(w.WrittenBytes());

  wlan_rx_info_t rx_info{.rx_flags = 0, .chan = kBssChannel};
  packet->CopyCtrlFrom(rx_info);

  return packet;
}

std::unique_ptr<Packet> CreateAmsduDataFramePacket(
    const std::vector<fbl::Span<const uint8_t>>& payloads) {
  common::MacAddr bssid(kBssid1);
  common::MacAddr client(kClientAddress);
  const uint8_t padding[]{0, 0, 0};
  fbl::Span<const uint8_t> padding_span(padding);

  size_t buf_len = DataFrameHeader::max_len();
  for (auto span : payloads) {
    buf_len += AmsduSubframeHeader::max_len() + LlcHeader::max_len() + span.size_bytes() + 3;
  };
  auto packet = GetWlanPacket(buf_len);
  ZX_DEBUG_ASSERT(packet != nullptr);

  BufferWriter w(*packet);
  auto data_hdr = w.Write<DataFrameHeader>();
  data_hdr->fc.set_type(FrameType::kData);
  data_hdr->fc.set_subtype(DataSubtype::kQosdata);
  data_hdr->fc.set_to_ds(0);
  data_hdr->fc.set_from_ds(1);
  data_hdr->addr1 = client;
  data_hdr->addr2 = bssid;
  data_hdr->addr3 = bssid;
  data_hdr->sc.set_val(42);
  auto qos_control = w.Write<QosControl>();
  qos_control->set_amsdu_present(1);

  for (auto i = 0ULL; i < payloads.size(); ++i) {
    auto msdu_hdr = w.Write<AmsduSubframeHeader>();
    msdu_hdr->da = client;
    msdu_hdr->sa = bssid;
    msdu_hdr->msdu_len_be = htobe16(LlcHeader::max_len() + payloads[i].size_bytes());

    auto llc_hdr = w.Write<LlcHeader>();
    llc_hdr->dsap = kLlcSnapExtension;
    llc_hdr->ssap = kLlcSnapExtension;
    llc_hdr->control = kLlcUnnumberedInformation;
    std::memcpy(llc_hdr->oui, kLlcOui, sizeof(llc_hdr->oui));
    llc_hdr->protocol_id_be = 42;
    w.Write(payloads[i]);
    if (i != payloads.size() - 1) {
      w.Write(padding_span.subspan(0, (6 - payloads[i].size_bytes()) % 4));
    }
  }

  packet->set_len(w.WrittenBytes());

  wlan_rx_info_t rx_info{.rx_flags = 0, .chan = kBssChannel};
  packet->CopyCtrlFrom(rx_info);

  return packet;
}

DataFrame<> CreateNullDataFrame() {
  common::MacAddr bssid(kBssid1);
  common::MacAddr client(kClientAddress);

  auto packet = GetWlanPacket(DataFrameHeader::max_len());
  ZX_DEBUG_ASSERT(packet != nullptr);

  BufferWriter w(*packet);
  auto data_hdr = w.Write<DataFrameHeader>();
  data_hdr->fc.set_type(FrameType::kData);
  data_hdr->fc.set_subtype(DataSubtype::kNull);
  data_hdr->fc.set_from_ds(1);
  data_hdr->addr1 = client;
  data_hdr->addr2 = bssid;
  data_hdr->addr3 = bssid;
  data_hdr->sc.set_val(42);

  packet->set_len(w.WrittenBytes());

  wlan_rx_info_t rx_info{.rx_flags = 0, .chan = kBssChannel};
  packet->CopyCtrlFrom(rx_info);

  return DataFrame<>(std::move(packet));
}

std::unique_ptr<Packet> CreateEthFrame(fbl::Span<const uint8_t> payload) {
  common::MacAddr bssid(kBssid1);
  common::MacAddr client(kClientAddress);

  size_t buf_len = EthernetII::max_len() + payload.size_bytes();
  auto packet = GetEthPacket(buf_len);
  ZX_DEBUG_ASSERT(packet != nullptr);

  BufferWriter w(*packet);
  auto eth_hdr = w.Write<EthernetII>();
  eth_hdr->src = client;
  eth_hdr->dest = bssid;
  eth_hdr->ether_type_be = 2;
  w.Write(payload);

  return packet;
}

}  // namespace wlan
