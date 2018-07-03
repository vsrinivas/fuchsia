// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <wlan/common/energy.h>
#include <wlan/common/macaddr.h>
#include <wlan/mlme/device_interface.h>
#include <wlan/mlme/mac_frame.h>
#include <wlan/mlme/packet.h>
#include <zircon/fidl.h>

#include <fuchsia/wlan/mlme/cpp/fidl.h>

#include <lib/fidl/cpp/decoder.h>
#include <lib/fidl/cpp/message.h>
#include <zircon/fidl.h>

namespace wlan {

namespace wlan_mlme = ::fuchsia::wlan::mlme;

template<typename T>
static zx_status_t SendServiceMsg(DeviceInterface* device, T* message, uint32_t ordinal) {
    // TODO(FIDL-2): replace this when we can get the size of the serialized response.
    size_t buf_len = 16384;
    fbl::unique_ptr<Buffer> buffer = GetBuffer(buf_len);
    if (buffer == nullptr) { return ZX_ERR_NO_RESOURCES; }

    auto packet = fbl::unique_ptr<Packet>(new Packet(std::move(buffer), buf_len));
    packet->set_peer(Packet::Peer::kService);
    zx_status_t status = SerializeServiceMsg(packet.get(), ordinal, message);
    if (status != ZX_OK) {
        errorf("could not serialize FIDL message: %d\n", status);
        return status;
    }
    return device->SendService(std::move(packet));
}

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

template <typename M> class MlmeMsg;

class BaseMlmeMsg {
   public:
    BaseMlmeMsg() = default;
    virtual ~BaseMlmeMsg() = default;

    template <typename M> const MlmeMsg<M>* As() const {
        return get_type_id() == MlmeMsg<M>::type_id() ? static_cast<const MlmeMsg<M>*>(this)
                                                      : nullptr;
    }

   protected:
    virtual const void* get_type_id() const = 0;

   private:
    BaseMlmeMsg(BaseMlmeMsg const&) = delete;
    BaseMlmeMsg& operator=(BaseMlmeMsg const&) = delete;
};

template <typename M> class MlmeMsg : public BaseMlmeMsg {
   public:
    static const uint8_t kTypeId = 0;
    ~MlmeMsg() override = default;

    static zx_status_t FromPacket(fbl::unique_ptr<Packet> pkt, MlmeMsg<M>* out_msg) {
        ZX_DEBUG_ASSERT(pkt != nullptr);

        auto hdr = FromBytes<fidl_message_header_t>(pkt->data(), pkt->len());
        if (hdr == nullptr) { return ZX_ERR_NOT_SUPPORTED; }

        out_msg->ordinal_ = hdr->ordinal;

        auto status = DeserializeServiceMsg(*pkt, hdr->ordinal, &out_msg->msg_);
        if (status != ZX_OK) { return status; }

        return ZX_OK;
    }

    // TODO(hahnr): ordinal() is only exposed while we transition to Frame Handling 2.0.
    // Once transition landed, MlmeMsg has no need even own the ordinal.
    uint32_t ordinal() const { return ordinal_; }

    const M* body() const { return &msg_; }

    static const void* type_id() { return &MlmeMsg<M>::kTypeId; }
    const void* get_type_id() const override { return type_id(); }

   private:
    uint32_t ordinal_;
    M msg_;
};

template <typename T> const uint8_t MlmeMsg<T>::kTypeId;

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
