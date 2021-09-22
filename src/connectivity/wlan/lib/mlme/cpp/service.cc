// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/wlan/ieee80211/cpp/fidl.h>

#include <wlan/mlme/service.h>

namespace wlan {
namespace service {

namespace wlan_ieee80211 = ::fuchsia::wlan::ieee80211;
namespace wlan_mlme = ::fuchsia::wlan::mlme;
namespace wlan_mesh = ::fuchsia::wlan::mesh;

std::optional<common::MacAddr> GetPeerAddr(const BaseMlmeMsg& msg) {
  if (auto auth_req = msg.As<wlan_mlme::AuthenticateRequest>()) {
    return std::make_optional<common::MacAddr>(auth_req->body()->peer_sta_address.data());
  } else if (auto assoc_req = msg.As<wlan_mlme::AssociateRequest>()) {
    return std::make_optional<common::MacAddr>(assoc_req->body()->peer_sta_address.data());
  } else if (auto deauth_req = msg.As<wlan_mlme::DeauthenticateRequest>()) {
    return std::make_optional<common::MacAddr>(deauth_req->body()->peer_sta_address.data());
  } else if (auto eapol_req = msg.As<wlan_mlme::EapolRequest>()) {
    return std::make_optional<common::MacAddr>(eapol_req->body()->dst_addr.data());
  } else if (auto auth_resp = msg.As<wlan_mlme::AuthenticateResponse>()) {
    return std::make_optional<common::MacAddr>(auth_resp->body()->peer_sta_address.data());
  } else if (auto assoc_resp = msg.As<wlan_mlme::AssociateResponse>()) {
    return std::make_optional<common::MacAddr>(assoc_resp->body()->peer_sta_address.data());
  } else if (auto open_req = msg.As<wlan_mlme::SetControlledPortRequest>()) {
    return std::make_optional<common::MacAddr>(open_req->body()->peer_sta_address.data());
  } else {
    return std::nullopt;
  }
}

zx_status_t SendAuthIndication(DeviceInterface* device, const common::MacAddr& peer_sta,
                               wlan_mlme::AuthenticationTypes auth_type) {
  debugfn();
  wlan_mlme::AuthenticateIndication ind;
  peer_sta.CopyTo(ind.peer_sta_address.data());
  ind.auth_type = auth_type;
  return SendServiceMsg(device, &ind, fuchsia::wlan::mlme::internal::kMLME_AuthenticateInd_Ordinal);
}

zx_status_t SendDeauthConfirm(DeviceInterface* device, const common::MacAddr& peer_sta) {
  debugfn();
  wlan_mlme::DeauthenticateConfirm conf;
  peer_sta.CopyTo(conf.peer_sta_address.data());
  return SendServiceMsg(device, &conf,
                        fuchsia::wlan::mlme::internal::kMLME_DeauthenticateConf_Ordinal);
}

zx_status_t SendDeauthIndication(DeviceInterface* device, const common::MacAddr& peer_sta,
                                 wlan_ieee80211::ReasonCode code) {
  debugfn();
  wlan_mlme::DeauthenticateIndication ind;
  peer_sta.CopyTo(ind.peer_sta_address.data());
  ind.reason_code = code;
  return SendServiceMsg(device, &ind,
                        fuchsia::wlan::mlme::internal::kMLME_DeauthenticateInd_Ordinal);
}

zx_status_t SendAssocIndication(DeviceInterface* device, const common::MacAddr& peer_sta,
                                uint16_t listen_interval, fbl::Span<const uint8_t> ssid,
                                std::optional<fbl::Span<const uint8_t>> rsne_body) {
  debugfn();
  wlan_mlme::AssociateIndication ind;
  peer_sta.CopyTo(ind.peer_sta_address.data());
  ind.listen_interval = listen_interval;
  ind.ssid.emplace();
  ind.ssid->assign(ssid.begin(), ssid.end());
  if (rsne_body) {
    ind.rsne.emplace(
        {static_cast<uint8_t>(element_id::kRsn), static_cast<uint8_t>(rsne_body->size())});
    ind.rsne->reserve(2 + rsne_body->size());
    ind.rsne->insert(ind.rsne->end(), rsne_body->begin(), rsne_body->end());
  }
  return SendServiceMsg(device, &ind, fuchsia::wlan::mlme::internal::kMLME_AssociateInd_Ordinal);
}

zx_status_t SendDisassociateIndication(DeviceInterface* device, const common::MacAddr& peer_sta,
                                       wlan_ieee80211::ReasonCode code) {
  debugfn();
  wlan_mlme::DisassociateIndication ind;
  peer_sta.CopyTo(ind.peer_sta_address.data());
  ind.reason_code = code;
  return SendServiceMsg(device, &ind, fuchsia::wlan::mlme::internal::kMLME_DisassociateInd_Ordinal);
}

zx_status_t SendSignalReportIndication(DeviceInterface* device, common::dBm rssi_dbm) {
  debugfn();
  ::fuchsia::wlan::internal::SignalReportIndication ind;
  ind.rssi_dbm = rssi_dbm.val;
  return SendServiceMsg(device, &ind, fuchsia::wlan::mlme::internal::kMLME_SignalReport_Ordinal);
}

zx_status_t SendEapolConfirm(DeviceInterface* device, wlan_mlme::EapolResultCode result_code) {
  debugfn();
  wlan_mlme::EapolConfirm resp;
  resp.result_code = result_code;
  return SendServiceMsg(device, &resp, fuchsia::wlan::mlme::internal::kMLME_EapolConf_Ordinal);
}

zx_status_t SendEapolIndication(DeviceInterface* device, const EapolHdr& eapol,
                                const common::MacAddr& src, const common::MacAddr& dst) {
  debugfn();

  // Limit EAPOL packet size. The EAPOL packet's size depends on the link
  // transport protocol and might exceed 255 octets. However, we don't support
  // EAP yet and EAPOL Key frames are always shorter.
  // TODO(hahnr): If necessary, find a better upper bound once we support EAP.
  size_t frame_len = eapol.len() + eapol.get_packet_body_length();
  if (frame_len > 255) {
    return ZX_OK;
  }

  wlan_mlme::EapolIndication ind;
  ind.data = ::std::vector<uint8_t>(frame_len);
  std::memcpy(ind.data.data(), &eapol, frame_len);
  src.CopyTo(ind.src_addr.data());
  dst.CopyTo(ind.dst_addr.data());
  return SendServiceMsg(device, &ind, fuchsia::wlan::mlme::internal::kMLME_EapolInd_Ordinal);
}

zx_status_t SendStartConfirm(DeviceInterface* device, wlan_mlme::StartResultCode code) {
  wlan_mlme::StartConfirm msg;
  msg.result_code = code;
  return SendServiceMsg(device, &msg, fuchsia::wlan::mlme::internal::kMLME_StartConf_Ordinal);
}

zx_status_t SendStopConfirm(DeviceInterface* device, wlan_mlme::StopResultCode code) {
  wlan_mlme::StopConfirm msg;
  msg.result_code = code;
  return SendServiceMsg(device, &msg, fuchsia::wlan::mlme::internal::kMLME_StopConf_Ordinal);
}

zx_status_t SendMeshPathTable(DeviceInterface* device, wlan_mesh::MeshPathTable& table,
                              uint64_t ordinal, zx_txid_t txid) {
  return SendServiceMsg(device, &table, ordinal, txid);
}

}  // namespace service
}  // namespace wlan
