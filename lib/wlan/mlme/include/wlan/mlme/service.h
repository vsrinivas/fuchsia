// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <wlan/common/macaddr.h>
#include <wlan/mlme/mac_frame.h>
#include <wlan/mlme/packet.h>

#include <fuchsia/cpp/wlan_mlme.h>

#include <lib/fidl/cpp/decoder.h>
#include <lib/fidl/cpp/message.h>

namespace wlan {

class DeviceInterface;

// ServiceHeader is the method header that is prepended to method calls over the channel.
// This will be removed when FIDL2 is available.
struct ServiceHeader {
  uint32_t txn_id;
  uint32_t reserved;
  uint32_t flags;
  uint32_t ordinal;
  uint8_t payload[];
} __PACKED;

template <typename T>
zx_status_t DeserializeServiceMsg(const Packet& packet, wlan_mlme::Method m, T* out) {
    if (out == nullptr) return ZX_ERR_INVALID_ARGS;

    auto h = packet.mut_field<ServiceHeader>(0);
    if (static_cast<wlan_mlme::Method>(h->ordinal) != m) return ZX_ERR_IO;

    auto payload = h->payload;
    size_t payload_len = packet.len() - sizeof(ServiceHeader);
    const char* err_msg = nullptr;
    zx_status_t status = fidl_decode(T::FidlType, payload, payload_len, nullptr, 0, &err_msg);
    if (status != ZX_OK) {
        errorf("could not decode received message: %s\n", err_msg);
        return status;
    }

    fidl::Message msg(fidl::BytePart(payload, payload_len, payload_len), fidl::HandlePart());
    fidl::Decoder decoder(std::move(msg));
    *out = std::move(fidl::DecodeAs<T>(&decoder, 0));
    return ZX_OK;
}

template <typename T> zx_status_t SerializeServiceMsg(Packet* packet, wlan_mlme::Method m, T* msg) {
    auto h = packet->mut_field<ServiceHeader>(0);
    h->ordinal = static_cast<uint32_t>(m);

    fidl::Encoder enc(static_cast<uint32_t>(m));
    enc.Alloc(fidl::CodingTraits<T>::encoded_size - sizeof(ServiceHeader));
    msg->Encode(&enc, 0);

    auto encoded = enc.GetMessage();
    const char* err_msg = nullptr;
    zx_status_t status = fidl_validate(T::FidlType, encoded.bytes().data(),
            encoded.bytes().actual(), 0, &err_msg);
    if (status != ZX_OK) {
        errorf("could not validate encoded message: %s\n", err_msg);
        return status;
    }

    status = packet->CopyFrom(encoded.bytes().data(), encoded.bytes().actual(),
            sizeof(ServiceHeader));
    if (status == ZX_OK) {
        packet->set_len(sizeof(ServiceHeader) + encoded.bytes().actual());
    }
    return status;
}

namespace service {

zx_status_t SendJoinResponse(DeviceInterface* device, wlan_mlme::JoinResultCodes result_code);
zx_status_t SendAuthResponse(DeviceInterface* device, const common::MacAddr& peer_sta,
                             wlan_mlme::AuthenticateResultCodes code);
zx_status_t SendDeauthResponse(DeviceInterface* device, const common::MacAddr& peer_sta);
zx_status_t SendDeauthIndication(DeviceInterface* device, const common::MacAddr& peer_sta,
                                 uint16_t code);
zx_status_t SendAssocResponse(DeviceInterface* device, wlan_mlme::AssociateResultCodes code,
                              uint16_t aid = 0);
zx_status_t SendDisassociateIndication(DeviceInterface* device, const common::MacAddr& peer_sta,
                                       uint16_t code);

zx_status_t SendSignalReportIndication(DeviceInterface* device, uint8_t rssi);

zx_status_t SendEapolResponse(DeviceInterface* device, wlan_mlme::EapolResultCodes result_code);
zx_status_t SendEapolIndication(DeviceInterface* device, const EapolFrame& eapol,
                                const common::MacAddr& src, const common::MacAddr& dst);
}  // namespace service

}  // namespace wlan
