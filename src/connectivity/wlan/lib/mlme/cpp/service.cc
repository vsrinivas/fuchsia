// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <wlan/mlme/service.h>

namespace wlan {
namespace service {

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

zx_status_t SendJoinConfirm(DeviceInterface* device, wlan_mlme::JoinResultCodes result_code) {
  debugfn();
  wlan_mlme::JoinConfirm conf;
  conf.result_code = result_code;
  return SendServiceMsg(device, &conf, fuchsia::wlan::mlme::internal::kMLME_JoinConf_GenOrdinal);
}

zx_status_t SendAuthConfirm(DeviceInterface* device, const common::MacAddr& peer_sta,
                            wlan_mlme::AuthenticateResultCodes code) {
  debugfn();
  wlan_mlme::AuthenticateConfirm conf;
  peer_sta.CopyTo(conf.peer_sta_address.data());
  // TODO(tkilbourn): set this based on the actual auth type
  conf.auth_type = wlan_mlme::AuthenticationTypes::OPEN_SYSTEM;
  conf.result_code = code;
  return SendServiceMsg(device, &conf,
                        fuchsia::wlan::mlme::internal::kMLME_AuthenticateConf_GenOrdinal);
}

zx_status_t SendAuthIndication(DeviceInterface* device, const common::MacAddr& peer_sta,
                               wlan_mlme::AuthenticationTypes auth_type) {
  debugfn();
  wlan_mlme::AuthenticateIndication ind;
  peer_sta.CopyTo(ind.peer_sta_address.data());
  ind.auth_type = auth_type;
  return SendServiceMsg(device, &ind, fuchsia::wlan::mlme::internal::kMLME_AuthenticateInd_GenOrdinal);
}

zx_status_t SendDeauthConfirm(DeviceInterface* device, const common::MacAddr& peer_sta) {
  debugfn();
  wlan_mlme::DeauthenticateConfirm conf;
  peer_sta.CopyTo(conf.peer_sta_address.data());
  return SendServiceMsg(device, &conf,
                        fuchsia::wlan::mlme::internal::kMLME_DeauthenticateConf_GenOrdinal);
}

zx_status_t SendDeauthIndication(DeviceInterface* device, const common::MacAddr& peer_sta,
                                 wlan_mlme::ReasonCode code) {
  debugfn();
  wlan_mlme::DeauthenticateIndication ind;
  peer_sta.CopyTo(ind.peer_sta_address.data());
  ind.reason_code = code;
  return SendServiceMsg(device, &ind,
                        fuchsia::wlan::mlme::internal::kMLME_DeauthenticateInd_GenOrdinal);
}

zx_status_t SendAssocConfirm(DeviceInterface* device, wlan_mlme::AssociateResultCodes code,
                             uint16_t aid) {
  debugfn();
  ZX_DEBUG_ASSERT(code != wlan_mlme::AssociateResultCodes::SUCCESS || aid != 0);

  wlan_mlme::AssociateConfirm conf;
  conf.result_code = code;
  conf.association_id = aid;
  return SendServiceMsg(device, &conf, fuchsia::wlan::mlme::internal::kMLME_AssociateConf_GenOrdinal);
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
  return SendServiceMsg(device, &ind, fuchsia::wlan::mlme::internal::kMLME_AssociateInd_GenOrdinal);
}

zx_status_t SendDisassociateIndication(DeviceInterface* device, const common::MacAddr& peer_sta,
                                       uint16_t code) {
  debugfn();
  wlan_mlme::DisassociateIndication ind;
  peer_sta.CopyTo(ind.peer_sta_address.data());
  ind.reason_code = code;
  return SendServiceMsg(device, &ind, fuchsia::wlan::mlme::internal::kMLME_DisassociateInd_GenOrdinal);
}

zx_status_t SendSignalReportIndication(DeviceInterface* device, common::dBm rssi_dbm) {
  debugfn();
  wlan_mlme::SignalReportIndication ind;
  ind.rssi_dbm = rssi_dbm.val;
  return SendServiceMsg(device, &ind, fuchsia::wlan::mlme::internal::kMLME_SignalReport_GenOrdinal);
}

zx_status_t SendEapolConfirm(DeviceInterface* device, wlan_mlme::EapolResultCodes result_code) {
  debugfn();
  wlan_mlme::EapolConfirm resp;
  resp.result_code = result_code;
  return SendServiceMsg(device, &resp, fuchsia::wlan::mlme::internal::kMLME_EapolConf_GenOrdinal);
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
  return SendServiceMsg(device, &ind, fuchsia::wlan::mlme::internal::kMLME_EapolInd_GenOrdinal);
}

zx_status_t SendStartConfirm(DeviceInterface* device, wlan_mlme::StartResultCodes code) {
  wlan_mlme::StartConfirm msg;
  msg.result_code = code;
  return SendServiceMsg(device, &msg, fuchsia::wlan::mlme::internal::kMLME_StartConf_GenOrdinal);
}

zx_status_t SendStopConfirm(DeviceInterface* device, wlan_mlme::StopResultCodes code) {
  wlan_mlme::StopConfirm msg;
  msg.result_code = code;
  return SendServiceMsg(device, &msg, fuchsia::wlan::mlme::internal::kMLME_StopConf_GenOrdinal);
}

zx_status_t SendMeshPathTable(DeviceInterface* device, wlan_mesh::MeshPathTable& table,
                              uint64_t ordinal, zx_txid_t txid) {
  return SendServiceMsg(device, &table, ordinal, txid);
}

}  // namespace service
}  // namespace wlan
