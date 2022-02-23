// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_FIDL_CPP_INCLUDE_LIB_FIDL_CPP_NATURAL_ENCODER_H_
#define SRC_LIB_FIDL_CPP_INCLUDE_LIB_FIDL_CPP_NATURAL_ENCODER_H_

#include <lib/fidl/cpp/message.h>
#include <lib/fidl/internal.h>
#include <lib/fidl/llcpp/message.h>

#ifdef __Fuchsia__
#include <lib/fidl/llcpp/internal/transport_channel.h>
#else
#include <lib/fidl/llcpp/internal/transport_channel_host.h>
#endif  // __Fuchsia__

#include <zircon/fidl.h>

#include <atomic>
#include <vector>

namespace fidl::internal {

class NaturalEncoder {
 public:
  NaturalEncoder(const CodingConfig* coding_config, fidl_handle_t* handles,
                 fidl_handle_metadata_t* handle_metadata, uint32_t handle_capacity);
  NaturalEncoder(const CodingConfig* coding_config, fidl_handle_t* handles,
                 fidl_handle_metadata_t* handle_metadata, uint32_t handle_capacity,
                 internal::WireFormatVersion wire_format);

  NaturalEncoder(NaturalEncoder&&) noexcept = default;
  NaturalEncoder& operator=(NaturalEncoder&&) noexcept = default;

  ~NaturalEncoder() = default;

  size_t Alloc(size_t size);

  template <typename T>
  T* GetPtr(size_t offset) {
    return reinterpret_cast<T*>(bytes_.data() + offset);
  }

  template <typename T>
  const T* GetPtr(size_t offset) const {
    return reinterpret_cast<const T*>(bytes_.data() + offset);
  }

  void EncodeHandle(fidl_handle_t handle, HandleAttributes attr, size_t offset);

  size_t CurrentLength() const { return bytes_.size(); }

  size_t CurrentHandleCount() const { return handle_actual_; }

  std::vector<uint8_t> TakeBytes() { return std::move(bytes_); }

  internal::WireFormatVersion wire_format() { return wire_format_; }

 protected:
  const CodingConfig* coding_config_;
  std::vector<uint8_t> bytes_;
  fidl_handle_t* handles_;
  fidl_handle_metadata_t* handle_metadata_;
  uint32_t handle_capacity_;
  uint32_t handle_actual_ = 0;
  internal::WireFormatVersion wire_format_ = internal::WireFormatVersion::kV2;
};

// The NaturalMessageEncoder produces an |HLCPPOutgoingMessage|, representing a transactional
// message.
template <typename Transport>
class NaturalMessageEncoder final : public NaturalEncoder {
 public:
  explicit NaturalMessageEncoder(uint64_t ordinal)
      : NaturalEncoder(&Transport::EncodingConfiguration, handles_,
                       reinterpret_cast<fidl_handle_metadata_t*>(handle_metadata_),
                       ZX_CHANNEL_MAX_MSG_HANDLES) {
    EncodeMessageHeader(ordinal);
  }

  NaturalMessageEncoder(NaturalMessageEncoder&&) noexcept = default;
  NaturalMessageEncoder& operator=(NaturalMessageEncoder&&) noexcept = default;

  ~NaturalMessageEncoder() = default;

  HLCPPOutgoingMessage GetMessage() {
    for (uint32_t i = 0; i < handle_actual_; i++) {
      handle_dispositions_[i] = zx_handle_disposition_t{
          .operation = ZX_HANDLE_OP_MOVE,
          .handle = handles_[i],
          .type = handle_metadata_[i].obj_type,
          .rights = handle_metadata_[i].rights,
          .result = ZX_OK,
      };
    }
    return HLCPPOutgoingMessage(
        BytePart(bytes_.data(), static_cast<uint32_t>(bytes_.size()),
                 static_cast<uint32_t>(bytes_.size())),
        HandleDispositionPart(handle_dispositions_, static_cast<uint32_t>(handle_actual_),
                              static_cast<uint32_t>(handle_actual_)));
  }

  void Reset(uint64_t ordinal) {
    bytes_.clear();
    handle_actual_ = 0;
    EncodeMessageHeader(ordinal);
  }

 private:
  fidl_handle_t handles_[ZX_CHANNEL_MAX_MSG_HANDLES];
  typename Transport::HandleMetadata handle_metadata_[ZX_CHANNEL_MAX_MSG_HANDLES];
  zx_handle_disposition_t handle_dispositions_[ZX_CHANNEL_MAX_MSG_HANDLES];

  void EncodeMessageHeader(uint64_t ordinal) {
    size_t offset = Alloc(sizeof(fidl_message_header_t));
    fidl_message_header_t* header = GetPtr<fidl_message_header_t>(offset);
    fidl_init_txn_header(header, 0, ordinal);
    if (wire_format() == internal::WireFormatVersion::kV2) {
      header->flags[0] |= FIDL_MESSAGE_HEADER_FLAGS_0_USE_VERSION_V2;
    }
  }
};

// The NaturalBodyEncoder produces an |HLCPPOutgoingBody|, representing a transactional message
// body.
class NaturalBodyEncoder final : public NaturalEncoder {
 public:
  explicit NaturalBodyEncoder(internal::WireFormatVersion wire_format)
      : NaturalEncoder(&fidl::internal::ChannelTransport::EncodingConfiguration, handles_,
                       reinterpret_cast<fidl_handle_metadata_t*>(handle_metadata_),
                       ZX_CHANNEL_MAX_MSG_HANDLES, wire_format) {}

  NaturalBodyEncoder(NaturalBodyEncoder&&) noexcept = default;
  NaturalBodyEncoder& operator=(NaturalBodyEncoder&&) noexcept = default;

  ~NaturalBodyEncoder() = default;

  fidl::OutgoingMessage GetBody();
  void Reset();

 private:
  fidl_handle_t handles_[ZX_CHANNEL_MAX_MSG_HANDLES];
  fidl_channel_handle_metadata_t handle_metadata_[ZX_CHANNEL_MAX_MSG_HANDLES];
};

}  // namespace fidl::internal

#endif  // SRC_LIB_FIDL_CPP_INCLUDE_LIB_FIDL_CPP_NATURAL_ENCODER_H_
