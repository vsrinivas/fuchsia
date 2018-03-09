// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <wlan/mlme/service.h>

#include <wlan/mlme/device_interface.h>

namespace wlan {

namespace service {

zx_status_t SendJoinResponse(DeviceInterface* device, JoinResultCodes result_code) {
    debugfn();

    auto resp = JoinResponse::New();
    resp->result_code = result_code;

    size_t buf_len = sizeof(ServiceHeader) + resp->GetSerializedSize();
    auto buffer = GetBuffer(buf_len);
    if (buffer == nullptr) { return ZX_ERR_NO_RESOURCES; }

    auto packet = fbl::make_unique<Packet>(fbl::move(buffer), buf_len);
    packet->set_peer(Packet::Peer::kService);
    auto status = SerializeServiceMsg(packet.get(), Method::JOIN_confirm, resp);
    if (status != ZX_OK) {
        errorf("could not serialize JoinResponse: %d\n", status);
    } else {
        status = device->SendService(fbl::move(packet));
    }

    return status;
}

zx_status_t SendAuthResponse(DeviceInterface* device, const common::MacAddr& peer_sta,
                             AuthenticateResultCodes code) {
    debugfn();

    auto resp = AuthenticateResponse::New();
    resp->peer_sta_address = f1dl::Array<uint8_t>::New(common::kMacAddrLen);
    peer_sta.CopyTo(resp->peer_sta_address.data());
    // TODO(tkilbourn): set this based on the actual auth type
    resp->auth_type = AuthenticationTypes::OPEN_SYSTEM;
    resp->result_code = code;

    size_t buf_len = sizeof(ServiceHeader) + resp->GetSerializedSize();
    auto buffer = GetBuffer(buf_len);
    if (buffer == nullptr) { return ZX_ERR_NO_RESOURCES; }

    auto packet = fbl::make_unique<Packet>(fbl::move(buffer), buf_len);
    packet->set_peer(Packet::Peer::kService);
    auto status = SerializeServiceMsg(packet.get(), Method::AUTHENTICATE_confirm, resp);
    if (status != ZX_OK) {
        errorf("could not serialize AuthenticateResponse: %d\n", status);
    } else {
        status = device->SendService(fbl::move(packet));
    }

    return status;
}

zx_status_t SendDeauthResponse(DeviceInterface* device, const common::MacAddr& peer_sta) {
    debugfn();

    auto resp = DeauthenticateResponse::New();
    resp->peer_sta_address = f1dl::Array<uint8_t>::New(common::kMacAddrLen);
    peer_sta.CopyTo(resp->peer_sta_address.data());

    size_t buf_len = sizeof(ServiceHeader) + resp->GetSerializedSize();
    auto buffer = GetBuffer(buf_len);
    if (buffer == nullptr) { return ZX_ERR_NO_RESOURCES; }

    auto packet = fbl::make_unique<Packet>(fbl::move(buffer), buf_len);
    packet->set_peer(Packet::Peer::kService);
    auto status = SerializeServiceMsg(packet.get(), Method::DEAUTHENTICATE_confirm, resp);
    if (status != ZX_OK) {
        errorf("could not serialize DeauthenticateResponse: %d\n", status);
    } else {
        status = device->SendService(fbl::move(packet));
    }

    return status;
}

zx_status_t SendDeauthIndication(DeviceInterface* device, const common::MacAddr& peer_sta,
                                 uint16_t code) {
    debugfn();

    auto ind = DeauthenticateIndication::New();
    ind->peer_sta_address = f1dl::Array<uint8_t>::New(common::kMacAddrLen);
    peer_sta.CopyTo(ind->peer_sta_address.data());
    ind->reason_code = code;

    size_t buf_len = sizeof(ServiceHeader) + ind->GetSerializedSize();
    auto buffer = GetBuffer(buf_len);
    if (buffer == nullptr) { return ZX_ERR_NO_RESOURCES; }

    auto packet = fbl::make_unique<Packet>(fbl::move(buffer), buf_len);
    packet->set_peer(Packet::Peer::kService);
    auto status = SerializeServiceMsg(packet.get(), Method::DEAUTHENTICATE_indication, ind);
    if (status != ZX_OK) {
        errorf("could not serialize DeauthenticateIndication: %d\n", status);
    } else {
        status = device->SendService(fbl::move(packet));
    }

    return status;
}

zx_status_t SendAssocResponse(DeviceInterface* device, AssociateResultCodes code, uint16_t aid) {
    debugfn();
    ZX_DEBUG_ASSERT(code != AssociateResultCodes::SUCCESS || aid != 0);

    auto resp = AssociateResponse::New();
    resp->result_code = code;
    resp->association_id = aid;

    size_t buf_len = sizeof(ServiceHeader) + resp->GetSerializedSize();
    auto buffer = GetBuffer(buf_len);
    if (buffer == nullptr) { return ZX_ERR_NO_RESOURCES; }

    auto packet = fbl::make_unique<Packet>(fbl::move(buffer), buf_len);
    packet->set_peer(Packet::Peer::kService);
    auto status = SerializeServiceMsg(packet.get(), Method::ASSOCIATE_confirm, resp);
    if (status != ZX_OK) {
        errorf("could not serialize AssociateResponse: %d\n", status);
    } else {
        status = device->SendService(fbl::move(packet));
    }

    return status;
}

zx_status_t SendDisassociateIndication(DeviceInterface* device, const common::MacAddr& peer_sta,
                                       uint16_t code) {
    debugfn();

    auto ind = DisassociateIndication::New();
    ind->peer_sta_address = f1dl::Array<uint8_t>::New(common::kMacAddrLen);
    peer_sta.CopyTo(ind->peer_sta_address.data());
    ind->reason_code = code;

    size_t buf_len = sizeof(ServiceHeader) + ind->GetSerializedSize();
    auto buffer = GetBuffer(buf_len);
    if (buffer == nullptr) { return ZX_ERR_NO_RESOURCES; }

    auto packet = fbl::make_unique<Packet>(fbl::move(buffer), buf_len);
    packet->set_peer(Packet::Peer::kService);
    auto status = SerializeServiceMsg(packet.get(), Method::DISASSOCIATE_indication, ind);
    if (status != ZX_OK) {
        errorf("could not serialize DisassociateIndication: %d\n", status);
    } else {
        status = device->SendService(fbl::move(packet));
    }

    return status;
}

zx_status_t SendSignalReportIndication(DeviceInterface* device, uint8_t rssi) {
    debugfn();

    auto ind = SignalReportIndication::New();
    ind->rssi = rssi;

    size_t buf_len = sizeof(ServiceHeader) + ind->GetSerializedSize();
    auto buffer = GetBuffer(buf_len);
    if (buffer == nullptr) { return ZX_ERR_NO_RESOURCES; }

    auto packet = fbl::make_unique<Packet>(fbl::move(buffer), buf_len);
    packet->set_peer(Packet::Peer::kService);
    auto status = SerializeServiceMsg(packet.get(), Method::SIGNAL_REPORT_indication, ind);
    if (status != ZX_OK) {
        errorf("could not serialize SignalReportIndication: %d\n", status);
    } else {
        status = device->SendService(fbl::move(packet));
    }

    return status;
}

zx_status_t SendEapolResponse(DeviceInterface* device, EapolResultCodes result_code) {
    debugfn();

    auto resp = EapolResponse::New();
    resp->result_code = result_code;

    size_t buf_len = sizeof(ServiceHeader) + resp->GetSerializedSize();
    auto buffer = GetBuffer(buf_len);
    if (buffer == nullptr) { return ZX_ERR_NO_RESOURCES; }

    auto packet = fbl::make_unique<Packet>(fbl::move(buffer), buf_len);
    packet->set_peer(Packet::Peer::kService);
    auto status = SerializeServiceMsg(packet.get(), Method::EAPOL_confirm, resp);
    if (status != ZX_OK) {
        errorf("could not serialize EapolResponse: %d\n", status);
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

    auto ind = EapolIndication::New();
    ind->data = ::f1dl::Array<uint8_t>::New(len);
    std::memcpy(ind->data.data(), &eapol, len);
    ind->src_addr = f1dl::Array<uint8_t>::New(common::kMacAddrLen);
    ind->dst_addr = f1dl::Array<uint8_t>::New(common::kMacAddrLen);
    src.CopyTo(ind->src_addr.data());
    dst.CopyTo(ind->dst_addr.data());

    size_t buf_len = sizeof(ServiceHeader) + ind->GetSerializedSize();
    auto buffer = GetBuffer(buf_len);
    if (buffer == nullptr) { return ZX_ERR_NO_RESOURCES; }

    auto packet = fbl::make_unique<Packet>(fbl::move(buffer), buf_len);
    packet->set_peer(Packet::Peer::kService);
    auto status = SerializeServiceMsg(packet.get(), Method::EAPOL_indication, ind);
    if (status != ZX_OK) {
        errorf("could not serialize MLME-Eapol.indication: %d\n", status);
    } else {
        status = device->SendService(fbl::move(packet));
    }
    return status;
}

}  // namespace service
}  // namespace wlan
