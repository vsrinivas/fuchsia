// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_WLAN_LIB_MLME_CPP_INCLUDE_WLAN_MLME_SERVICE_H_
#define SRC_CONNECTIVITY_WLAN_LIB_MLME_CPP_INCLUDE_WLAN_MLME_SERVICE_H_

#include <fuchsia/wlan/mlme/cpp/fidl.h>
#include <lib/fidl/cpp/decoder.h>
#include <lib/fidl/cpp/message.h>
#include <zircon/fidl.h>

#include <fbl/span.h>
#include <wlan/common/buffer_reader.h>
#include <wlan/common/energy.h>
#include <wlan/common/macaddr.h>
#include <wlan/mlme/device_interface.h>
#include <wlan/mlme/mac_frame.h>
#include <wlan/mlme/packet.h>

namespace wlan {

template <typename T>
zx_status_t SerializeServiceMsg(fidl::Encoder* enc, T* msg, zx_txid_t txid = 0) {
  // Encode our message of type T. The encoder will take care of extending the
  // buffer to accommodate out-of-line data (e.g., vectors, strings, and
  // nullable data).
  enc->Alloc(fidl::EncodingInlineSize<T>(enc));
  msg->Encode(enc, sizeof(fidl_message_header_t));

  // The coding tables for fidl structs do not include offsets for the message
  // header, so we must run validation starting after this header.
  auto encoded = enc->GetMessage();
  encoded.set_txid(txid);

  auto msg_body = encoded.payload();
  const char* err_msg = nullptr;
  zx_status_t status = fidl_validate(T::FidlType, msg_body.data(), msg_body.size(), 0, &err_msg);
  if (status != ZX_OK) {
    errorf("could not validate encoded message: %s\n", err_msg);
  }
  return status;
}

template <typename T>
static zx_status_t SendServiceMsg(DeviceInterface* device, T* message, uint64_t ordinal,
                                  zx_txid_t txid = 0) {
  fidl::Encoder enc(ordinal);

  zx_status_t status = SerializeServiceMsg(&enc, message, txid);
  if (status != ZX_OK) {
    errorf("could not serialize FIDL message %lu: %d\n", ordinal, status);
    return status;
  }
  return device->SendService(enc.GetMessage().bytes());
}

template <typename M>
class MlmeMsg;

class BaseMlmeMsg {
 public:
  virtual ~BaseMlmeMsg() = default;

  template <typename M>
  const MlmeMsg<M>* As() const {
    return get_type_id() == MlmeMsg<M>::type_id() ? static_cast<const MlmeMsg<M>*>(this) : nullptr;
  }

  zx_txid_t txid() const { return txid_; }
  uint64_t ordinal() const { return ordinal_; }

 protected:
  BaseMlmeMsg(uint64_t ordinal, zx_txid_t txid) : ordinal_(ordinal), txid_(txid) {}
  BaseMlmeMsg(BaseMlmeMsg&&) = default;
  virtual const void* get_type_id() const = 0;

  uint64_t ordinal_ = 0;
  zx_txid_t txid_ = 0;

 private:
  BaseMlmeMsg(BaseMlmeMsg const&) = delete;
  BaseMlmeMsg& operator=(BaseMlmeMsg const&) = delete;
};

template <typename M>
class MlmeMsg : public BaseMlmeMsg {
 public:
  static const uint8_t kTypeId = 0;
  MlmeMsg(M&& msg, uint64_t ordinal, zx_txid_t txid = 0)
      : BaseMlmeMsg(ordinal, txid), msg_(std::move(msg)) {}
  MlmeMsg(MlmeMsg&&) = default;
  ~MlmeMsg() override = default;

  static constexpr uint64_t kNoOrdinal = 0;  // Not applicable or does not matter
  static std::optional<MlmeMsg<M>> Decode(fbl::Span<uint8_t> span, uint64_t ordinal = kNoOrdinal) {
    BufferReader reader(span);
    auto h = reader.Read<fidl_message_header_t>();
    if (h == nullptr) {
      errorf("MLME message too short\n");
      return {};
    }

    if (ordinal != kNoOrdinal && ordinal != h->ordinal) {
      // Generated code uses hexadecimal to represent ordinal
      warnf("Mismatched ordinal: expected: 0x%0lx, actual: 0x%0lx\n", ordinal, h->ordinal);
      return {};
    }

    // Extract the message contents and decode in-place (i.e., fixup all the
    // out-of-line pointers to be offsets into the span).
    auto payload = span.subspan(reader.ReadBytes());
    const char* err_msg = nullptr;
    auto status = fidl_decode(M::FidlType, payload.data(), payload.size(), nullptr, 0, &err_msg);
    if (status != ZX_OK) {
      errorf("could not decode received message: %s\n", err_msg);
      return {};
    }

    // Construct a fidl Message and decode it into M.
    fidl::Message msg(fidl::BytePart(payload.data(), payload.size(), payload.size()),
                      fidl::HandlePart());
    fidl::Decoder decoder(std::move(msg));
    return {{fidl::DecodeAs<M>(&decoder, 0), h->ordinal, h->txid}};
  }

  const M* body() const { return &msg_; }
  const M cloned_body() const {
    M msg;
    msg_.Clone(&msg);
    return msg;
  }

  static const void* type_id() { return &MlmeMsg<M>::kTypeId; }
  const void* get_type_id() const override { return type_id(); }

 private:
  M msg_;
};

template <typename T>
const uint8_t MlmeMsg<T>::kTypeId;

namespace service {

// Returns the peer MAC address for messages which carry one, none otherwise.
std::optional<common::MacAddr> GetPeerAddr(const BaseMlmeMsg& msg);
zx_status_t SendJoinConfirm(DeviceInterface* device,
                            ::fuchsia::wlan::mlme::JoinResultCodes result_code);
zx_status_t SendAuthConfirm(DeviceInterface* device, const common::MacAddr& peer_sta,
                            ::fuchsia::wlan::mlme::AuthenticateResultCodes code);
zx_status_t SendAuthIndication(DeviceInterface* device, const common::MacAddr& peer_sta,
                               ::fuchsia::wlan::mlme::AuthenticationTypes auth_type);
zx_status_t SendDeauthConfirm(DeviceInterface* device, const common::MacAddr& peer_sta);
zx_status_t SendDeauthIndication(DeviceInterface* device, const common::MacAddr& peer_sta,
                                 ::fuchsia::wlan::mlme::ReasonCode code);
zx_status_t SendAssocConfirm(DeviceInterface* device,
                             ::fuchsia::wlan::mlme::AssociateResultCodes code, uint16_t aid = 0);
zx_status_t SendAssocIndication(DeviceInterface* device, const common::MacAddr& peer_sta,
                                uint16_t listen_interval, fbl::Span<const uint8_t> ssid,
                                std::optional<fbl::Span<const uint8_t>> rsn_body);
zx_status_t SendDisassociateIndication(DeviceInterface* device, const common::MacAddr& peer_sta,
                                       uint16_t code);

zx_status_t SendSignalReportIndication(DeviceInterface* device, common::dBm rssi_dbm);

zx_status_t SendEapolConfirm(DeviceInterface* device,
                             ::fuchsia::wlan::mlme::EapolResultCodes result_code);
zx_status_t SendEapolIndication(DeviceInterface* device, const EapolHdr& eapol,
                                const common::MacAddr& src, const common::MacAddr& dst);

zx_status_t SendStartConfirm(DeviceInterface* device, ::fuchsia::wlan::mlme::StartResultCodes code);
zx_status_t SendStopConfirm(DeviceInterface* device, ::fuchsia::wlan::mlme::StopResultCodes code);
zx_status_t SendMeshPathTable(DeviceInterface* device, ::fuchsia::wlan::mesh::MeshPathTable& table,
                              uint64_t ordinal, zx_txid_t txid);

}  // namespace service

}  // namespace wlan

#endif  // SRC_CONNECTIVITY_WLAN_LIB_MLME_CPP_INCLUDE_WLAN_MLME_SERVICE_H_
