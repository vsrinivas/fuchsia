// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_WLAN_LIB_MLME_CPP_TESTS_MLME_MSG_H_
#define SRC_CONNECTIVITY_WLAN_LIB_MLME_CPP_TESTS_MLME_MSG_H_

#include <fuchsia/wlan/ieee80211/cpp/fidl.h>
#include <fuchsia/wlan/mlme/cpp/fidl.h>
#include <lib/fidl/cpp/decoder.h>
#include <lib/fidl/cpp/message.h>
#include <zircon/fidl.h>

#include <wlan/common/buffer_reader.h>
#include <wlan/common/energy.h>
#include <wlan/common/macaddr.h>
#include <wlan/mlme/device_interface.h>
#include <wlan/mlme/mac_frame.h>
#include <wlan/mlme/packet.h>

#include "lib/fidl/llcpp/traits.h"

namespace wlan {
template <typename M>
class MlmeMsg;

class BaseMlmeMsg {
 public:
  virtual ~BaseMlmeMsg() = default;

  template <typename M>
  const MlmeMsg<M>* As() const {
    return static_cast<const MlmeMsg<M>*>(this);
  }

  zx_txid_t txid() const { return txid_; }
  uint64_t ordinal() const { return ordinal_; }

 protected:
  BaseMlmeMsg(uint64_t ordinal, zx_txid_t txid) : ordinal_(ordinal), txid_(txid) {}
  BaseMlmeMsg(BaseMlmeMsg&&) = default;

  uint64_t ordinal_ = 0;
  zx_txid_t txid_ = 0;

 private:
  BaseMlmeMsg(BaseMlmeMsg const&) = delete;
  BaseMlmeMsg& operator=(BaseMlmeMsg const&) = delete;
};

template <typename M>
class MlmeMsg : public BaseMlmeMsg {
 public:
  MlmeMsg(M&& msg, uint64_t ordinal, zx_txid_t txid = 0)
      : BaseMlmeMsg(ordinal, txid), msg_(std::move(msg)) {}
  MlmeMsg(MlmeMsg&&) = default;
  ~MlmeMsg() override = default;

  static constexpr uint64_t kNoOrdinal = 0;  // Not applicable or does not matter
  static std::optional<MlmeMsg<M>> Decode(cpp20::span<uint8_t> span,
                                          uint64_t ordinal = kNoOrdinal) {
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

    // Construct a fidl message body and decode it into M.
    fidl::HLCPPIncomingBody body(fidl::BytePart(payload.data(), payload.size(), payload.size()),
                                 fidl::HandleInfoPart());
    const char* err_msg = nullptr;
    // TODO(fxbug.dev/82681): This uses an internal type to decode the payload based on flags
    // specified in the FIDL message header. Move to public API when FIDL-at-rest is ready.
    zx_status_t status = body.Decode(
        ::fidl::internal::WireFormatMetadata::FromTransactionalHeader(*h), M::FidlType, &err_msg);
    if (status != ZX_OK) {
      errorf("could not decode received message: %s\n", err_msg);
      return {};
    }
    fidl::Decoder decoder(std::move(body));
    return {{fidl::DecodeAs<M>(&decoder, 0), h->ordinal, h->txid}};
  }

  const M* body() const { return &msg_; }
  const M cloned_body() const {
    M msg;
    msg_.Clone(&msg);
    return msg;
  }

 private:
  M msg_;
};
}  // namespace wlan

#endif  // SRC_CONNECTIVITY_WLAN_LIB_MLME_CPP_TESTS_MLME_MSG_H_
