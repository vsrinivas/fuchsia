// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <wlan/common/energy.h>
#include <wlan/common/macaddr.h>
#include <wlan/mlme/mac_frame.h>
#include <wlan/mlme/packet.h>
#include <zircon/fidl.h>

#include <wlan_mlme/cpp/fidl.h>

#include <lib/fidl/cpp/decoder.h>
#include <lib/fidl/cpp/message.h>

namespace wlan {

class DeviceInterface;

template <typename T>
zx_status_t DeserializeServiceMsg(const Packet& packet, uint32_t ordinal, T* out) {
    if (out == nullptr) return ZX_ERR_INVALID_ARGS;

    // Verify that the message header contains the ordinal we expect.
    auto h = packet.mut_field<fidl_message_header_t>(0);
    if (h->ordinal != ordinal) return ZX_ERR_IO;

    // Extract the message contents and decode in-place (i.e., fixup all the out-of-line pointers to
    // be offsets into the buffer).
    auto payload = packet.mut_field<uint8_t>(sizeof(fidl_message_header_t));
    size_t payload_len = packet.len() - sizeof(fidl_message_header_t);
    const char* err_msg = nullptr;
    zx_status_t status = fidl_decode(T::FidlType, payload, payload_len, nullptr, 0, &err_msg);
    if (status != ZX_OK) {
        errorf("could not decode received message: %s\n", err_msg);
        return status;
    }

    // Construct a fidl Message and decode it into our T.
    fidl::Message msg(fidl::BytePart(payload, payload_len, payload_len), fidl::HandlePart());
    fidl::Decoder decoder(std::move(msg));
    *out = std::move(fidl::DecodeAs<T>(&decoder, 0));
    return ZX_OK;
}

template <typename T> zx_status_t SerializeServiceMsg(Packet* packet, uint32_t ordinal, T* msg) {
    // Create an encoder that sets the ordinal to m.
    fidl::Encoder enc(ordinal);

    // Encode our message of type T. The encoder will take care of extending the buffer to
    // accommodate out-of-line data (e.g., vectors, strings, and nullable data).
    enc.Alloc(fidl::CodingTraits<T>::encoded_size);
    msg->Encode(&enc, sizeof(fidl_message_header_t));

    // The coding tables for fidl structs do not include offsets for the message header, so we must
    // run validation starting after this header.
    auto encoded = enc.GetMessage();
    ZX_ASSERT(encoded.bytes().actual() >= sizeof(fidl_message_header_t));
    const void* msg_bytes = encoded.bytes().data() + sizeof(fidl_message_header_t);
    uint32_t msg_actual = encoded.bytes().actual() - sizeof(fidl_message_header_t);
    const char* err_msg = nullptr;
    zx_status_t status = fidl_validate(T::FidlType, msg_bytes, msg_actual, 0, &err_msg);
    if (status != ZX_OK) {
        errorf("could not validate encoded message: %s\n", err_msg);
        return status;
    }

    // Copy all of the encoded data, including the header, into the packet.
    status = packet->CopyFrom(encoded.bytes().data(), encoded.bytes().actual(), 0);
    if (status == ZX_OK) {
        // We must set the length ourselves, since the initial packet length was almost certainly an
        // over-estimate.
        packet->set_len(encoded.bytes().actual());
    }
    return status;
}

namespace service {

zx_status_t SendJoinConfirm(DeviceInterface* device, wlan_mlme::JoinResultCodes result_code);
zx_status_t SendAuthConfirm(DeviceInterface* device, const common::MacAddr& peer_sta,
                            wlan_mlme::AuthenticateResultCodes code);
zx_status_t SendDeauthConfirm(DeviceInterface* device, const common::MacAddr& peer_sta);
zx_status_t SendDeauthIndication(DeviceInterface* device, const common::MacAddr& peer_sta,
                                 wlan_mlme::ReasonCode code);
zx_status_t SendAssocConfirm(DeviceInterface* device, wlan_mlme::AssociateResultCodes code,
                             uint16_t aid = 0);
zx_status_t SendDisassociateIndication(DeviceInterface* device, const common::MacAddr& peer_sta,
                                       uint16_t code);

zx_status_t SendSignalReportIndication(DeviceInterface* device, common::dBm rssi_dbm);

zx_status_t SendEapolConfirm(DeviceInterface* device, wlan_mlme::EapolResultCodes result_code);
zx_status_t SendEapolIndication(DeviceInterface* device, const EapolFrame& eapol,
                                const common::MacAddr& src, const common::MacAddr& dst);
}  // namespace service

}  // namespace wlan
