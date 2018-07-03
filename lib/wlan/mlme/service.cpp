// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <wlan/mlme/service.h>

#include <fuchsia/wlan/mlme/c/fidl.h>

namespace wlan {
namespace service {

namespace wlan_mlme = ::fuchsia::wlan::mlme;

zx_status_t SendJoinConfirm(DeviceInterface* device, wlan_mlme::JoinResultCodes result_code) {
    debugfn();

    auto resp = wlan_mlme::JoinConfirm::New();
    resp->result_code = result_code;

    // fidl2 doesn't have a way to get the serialized size yet. 4096 bytes should be enough for
    // everyone.
    size_t buf_len = 4096;
    auto buffer = GetBuffer(buf_len);
    if (buffer == nullptr) { return ZX_ERR_NO_RESOURCES; }

    auto packet = fbl::make_unique<Packet>(fbl::move(buffer), buf_len);
    packet->set_peer(Packet::Peer::kService);
    auto status = SerializeServiceMsg(packet.get(), fuchsia_wlan_mlme_MLMEJoinConfOrdinal, resp.get());
    if (status != ZX_OK) {
        errorf("could not serialize JoinConfirm: %d\n", status);
    } else {
        status = device->SendService(fbl::move(packet));
    }

    return status;
}

zx_status_t SendAuthConfirm(DeviceInterface* device, const common::MacAddr& peer_sta,
                            wlan_mlme::AuthenticateResultCodes code) {
    debugfn();

    auto resp = wlan_mlme::AuthenticateConfirm::New();
    peer_sta.CopyTo(resp->peer_sta_address.mutable_data());
    // TODO(tkilbourn): set this based on the actual auth type
    resp->auth_type = wlan_mlme::AuthenticationTypes::OPEN_SYSTEM;
    resp->result_code = code;

    // fidl2 doesn't have a way to get the serialized size yet. 4096 bytes should be enough for
    // everyone.
    size_t buf_len = 4096;
    auto buffer = GetBuffer(buf_len);
    if (buffer == nullptr) { return ZX_ERR_NO_RESOURCES; }

    auto packet = fbl::make_unique<Packet>(fbl::move(buffer), buf_len);
    packet->set_peer(Packet::Peer::kService);
    auto status =
        SerializeServiceMsg(packet.get(), fuchsia_wlan_mlme_MLMEAuthenticateConfOrdinal, resp.get());
    if (status != ZX_OK) {
        errorf("could not serialize AuthenticateConfirm: %d\n", status);
    } else {
        status = device->SendService(fbl::move(packet));
    }

    return status;
}

zx_status_t SendDeauthConfirm(DeviceInterface* device, const common::MacAddr& peer_sta) {
    debugfn();

    auto resp = wlan_mlme::DeauthenticateConfirm::New();
    peer_sta.CopyTo(resp->peer_sta_address.mutable_data());

    // fidl2 doesn't have a way to get the serialized size yet. 4096 bytes should be enough for
    // everyone.
    size_t buf_len = 4096;
    auto buffer = GetBuffer(buf_len);
    if (buffer == nullptr) { return ZX_ERR_NO_RESOURCES; }

    auto packet = fbl::make_unique<Packet>(fbl::move(buffer), buf_len);
    packet->set_peer(Packet::Peer::kService);
    auto status =
        SerializeServiceMsg(packet.get(), fuchsia_wlan_mlme_MLMEDeauthenticateConfOrdinal, resp.get());
    if (status != ZX_OK) {
        errorf("could not serialize DeauthenticateConfirm: %d\n", status);
    } else {
        status = device->SendService(fbl::move(packet));
    }

    return status;
}

zx_status_t SendDeauthIndication(DeviceInterface* device, const common::MacAddr& peer_sta,
                                 wlan_mlme::ReasonCode code) {
    debugfn();

    auto ind = wlan_mlme::DeauthenticateIndication::New();
    peer_sta.CopyTo(ind->peer_sta_address.mutable_data());
    ind->reason_code = code;

    // fidl2 doesn't have a way to get the serialized size yet. 4096 bytes should be enough for
    // everyone.
    size_t buf_len = 4096;
    auto buffer = GetBuffer(buf_len);
    if (buffer == nullptr) { return ZX_ERR_NO_RESOURCES; }

    auto packet = fbl::make_unique<Packet>(fbl::move(buffer), buf_len);
    packet->set_peer(Packet::Peer::kService);
    auto status =
        SerializeServiceMsg(packet.get(), fuchsia_wlan_mlme_MLMEDeauthenticateIndOrdinal, ind.get());
    if (status != ZX_OK) {
        errorf("could not serialize DeauthenticateIndication: %d\n", status);
    } else {
        status = device->SendService(fbl::move(packet));
    }

    return status;
}

zx_status_t SendAssocConfirm(DeviceInterface* device, wlan_mlme::AssociateResultCodes code,
                             uint16_t aid) {
    debugfn();
    ZX_DEBUG_ASSERT(code != wlan_mlme::AssociateResultCodes::SUCCESS || aid != 0);

    auto resp = wlan_mlme::AssociateConfirm::New();
    resp->result_code = code;
    resp->association_id = aid;

    // fidl2 doesn't have a way to get the serialized size yet. 4096 bytes should be enough for
    // everyone.
    size_t buf_len = 4096;
    auto buffer = GetBuffer(buf_len);
    if (buffer == nullptr) { return ZX_ERR_NO_RESOURCES; }

    auto packet = fbl::make_unique<Packet>(fbl::move(buffer), buf_len);
    packet->set_peer(Packet::Peer::kService);
    auto status = SerializeServiceMsg(packet.get(), fuchsia_wlan_mlme_MLMEAssociateConfOrdinal, resp.get());
    if (status != ZX_OK) {
        errorf("could not serialize AssociateConfirm: %d\n", status);
    } else {
        status = device->SendService(fbl::move(packet));
    }

    return status;
}

zx_status_t SendDisassociateIndication(DeviceInterface* device, const common::MacAddr& peer_sta,
                                       uint16_t code) {
    debugfn();

    auto ind = wlan_mlme::DisassociateIndication::New();
    peer_sta.CopyTo(ind->peer_sta_address.mutable_data());
    ind->reason_code = code;

    // fidl2 doesn't have a way to get the serialized size yet. 4096 bytes should be enough for
    // everyone.
    size_t buf_len = 4096;
    auto buffer = GetBuffer(buf_len);
    if (buffer == nullptr) { return ZX_ERR_NO_RESOURCES; }

    auto packet = fbl::make_unique<Packet>(fbl::move(buffer), buf_len);
    packet->set_peer(Packet::Peer::kService);
    auto status =
        SerializeServiceMsg(packet.get(), fuchsia_wlan_mlme_MLMEDisassociateIndOrdinal, ind.get());
    if (status != ZX_OK) {
        errorf("could not serialize DisassociateIndication: %d\n", status);
    } else {
        status = device->SendService(fbl::move(packet));
    }

    return status;
}

zx_status_t SendSignalReportIndication(DeviceInterface* device, common::dBm rssi_dbm) {
    debugfn();

    auto ind = wlan_mlme::SignalReportIndication::New();
    ind->rssi_dbm = rssi_dbm.val;

    // fidl2 doesn't have a way to get the serialized size yet. 4096 bytes should be enough for
    // everyone.
    size_t buf_len = 4096;
    auto buffer = GetBuffer(buf_len);
    if (buffer == nullptr) { return ZX_ERR_NO_RESOURCES; }

    auto packet = fbl::make_unique<Packet>(fbl::move(buffer), buf_len);
    packet->set_peer(Packet::Peer::kService);
    auto status = SerializeServiceMsg(packet.get(), fuchsia_wlan_mlme_MLMESignalReportOrdinal, ind.get());
    if (status != ZX_OK) {
        errorf("could not serialize SignalReportIndication: %d\n", status);
    } else {
        status = device->SendService(fbl::move(packet));
    }

    return status;
}

zx_status_t SendEapolConfirm(DeviceInterface* device, wlan_mlme::EapolResultCodes result_code) {
    debugfn();

    auto resp = wlan_mlme::EapolConfirm::New();
    resp->result_code = result_code;

    // fidl2 doesn't have a way to get the serialized size yet. 4096 bytes should be enough for
    // everyone.
    size_t buf_len = 4096;
    auto buffer = GetBuffer(buf_len);
    if (buffer == nullptr) { return ZX_ERR_NO_RESOURCES; }

    auto packet = fbl::make_unique<Packet>(fbl::move(buffer), buf_len);
    packet->set_peer(Packet::Peer::kService);
    auto status = SerializeServiceMsg(packet.get(), fuchsia_wlan_mlme_MLMEEapolConfOrdinal, resp.get());
    if (status != ZX_OK) {
        errorf("could not serialize EapolConfirm: %d\n", status);
    } else {
        status = device->SendService(fbl::move(packet));
    }
    return status;
}

zx_status_t SendEapolIndication(DeviceInterface* device, const EapolFrame& eapol,
                                const common::MacAddr& src, const common::MacAddr& dst) {
    debugfn();

    // Limit EAPOL packet size. The EAPOL packet's size depends on the link transport protocol and
    // might exceed 255 octets. However, we don't support EAP yet and EAPOL Key frames are always
    // shorter.
    // TODO(hahnr): If necessary, find a better upper bound once we support EAP.
    size_t len = sizeof(EapolFrame) + be16toh(eapol.packet_body_length);
    if (len > 255) { return ZX_OK; }

    auto ind = wlan_mlme::EapolIndication::New();
    ind->data = ::fidl::VectorPtr<uint8_t>::New(len);
    std::memcpy(ind->data->data(), &eapol, len);
    src.CopyTo(ind->src_addr.mutable_data());
    dst.CopyTo(ind->dst_addr.mutable_data());

    // fidl2 doesn't have a way to get the serialized size yet. 4096 bytes should be enough for
    // everyone.
    size_t buf_len = 4096;
    auto buffer = GetBuffer(buf_len);
    if (buffer == nullptr) { return ZX_ERR_NO_RESOURCES; }

    auto packet = fbl::make_unique<Packet>(fbl::move(buffer), buf_len);
    packet->set_peer(Packet::Peer::kService);
    auto status = SerializeServiceMsg(packet.get(), fuchsia_wlan_mlme_MLMEEapolIndOrdinal, ind.get());
    if (status != ZX_OK) {
        errorf("could not serialize MLME-Eapol.indication: %d\n", status);
    } else {
        status = device->SendService(fbl::move(packet));
    }
    return status;
}

}  // namespace service
}  // namespace wlan
