// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_FIDL_CPP_INCLUDE_LIB_FIDL_CPP_NATURAL_DECODER_H_
#define SRC_LIB_FIDL_CPP_INCLUDE_LIB_FIDL_CPP_NATURAL_DECODER_H_

#include <lib/fidl/llcpp/message.h>
#include <zircon/fidl.h>

#ifdef __Fuchsia__
#include <lib/fidl/llcpp/internal/transport_channel.h>
#include <lib/zx/handle.h>
#include <lib/zx/object.h>
#else
#include <lib/fidl/llcpp/internal/transport_channel_host.h>
#endif

namespace fidl::internal {

class NaturalDecoder final {
 public:
  explicit NaturalDecoder(fidl::IncomingMessage message,
                          fidl::internal::WireFormatVersion wire_format_version);
  ~NaturalDecoder();

  template <typename T>
  T* GetPtr(size_t offset) {
    ZX_DEBUG_ASSERT(offset <= body_.byte_actual());
    return reinterpret_cast<T*>((body_.bytes() - body_offset_) + offset);
  }

  size_t GetOffset(const void* ptr) const { return GetOffset(reinterpret_cast<uintptr_t>(ptr)); }
  size_t GetOffset(uintptr_t ptr) const {
    // The |ptr| value comes from the message buffer, which we've already
    // validated. That means it should correspond to a valid offset within the
    // message.
    size_t offset = ptr - reinterpret_cast<uintptr_t>(body_.bytes() - body_offset_);
    ZX_DEBUG_ASSERT(offset <= body_.byte_actual());
    return offset;
  }

  [[nodiscard]] bool Alloc(size_t size, size_t* offset) {
    if (size > std::numeric_limits<uint32_t>::max()) {
      SetError("allocation size exceeds 32-bits");
      return false;
    }
    size_t old = next_out_of_line_;
    size_t next = next_out_of_line_ + FIDL_ALIGN(size);
    if (next > body_.byte_actual()) {
      SetError("out of line object exceeds message bounds");
      return false;
    }
    next_out_of_line_ = next;
    *offset = old;
    return true;
  }

#ifdef __Fuchsia__
  void DecodeHandle(zx::object_base* value, HandleAttributes attr, size_t offset) {
    zx_handle_t* handle = GetPtr<zx_handle_t>(offset);
    switch (*handle) {
      case FIDL_HANDLE_PRESENT: {
        ZX_DEBUG_ASSERT(handle_index_ < body_.handle_actual());
        zx_handle_t& body_handle = body_.handles()[handle_index_];

        const char* error;
        zx_status_t status =
            fidl::internal::ChannelTransport::EncodingConfiguration.decode_process_handle(
                &body_handle, attr, handle_index_,
                body_.handle_metadata<fidl::internal::ChannelTransport>(), &error);
        if (status != ZX_OK) {
          SetError(error);
          return;
        }

        value->reset(body_handle);
        body_handle = ZX_HANDLE_INVALID;
        ++handle_index_;
        return;
      }
      case FIDL_HANDLE_ABSENT: {
        value->reset();
        return;
      }
      default: {
        SetError("invalid presence marker");
        return;
      }
    }
  }
#endif

  struct EnvelopeUnknownDataInfoResult {
    size_t value_offset;
    uint32_t num_bytes;
    uint16_t num_handles;
    uint16_t flags;
  };

  EnvelopeUnknownDataInfoResult EnvelopeUnknownDataInfo(const fidl_envelope_v2_t* envelope) const {
    const auto* unknown_data_envelope =
        reinterpret_cast<const fidl_envelope_v2_unknown_data_t*>(envelope);

    EnvelopeUnknownDataInfoResult result;
    if ((unknown_data_envelope->flags & FIDL_ENVELOPE_FLAGS_INLINING_MASK) != 0) {
      result.value_offset = GetOffset(&envelope->inline_value);
      result.num_bytes = 4;
    } else {
      result.value_offset = unknown_data_envelope->out_of_line.offset;
      result.num_bytes = unknown_data_envelope->out_of_line.num_bytes;
    }
    result.num_handles = unknown_data_envelope->num_handles;
    result.flags = unknown_data_envelope->flags;

    return result;
  }

  void SetError(const char* error) {
    if (status_ != ZX_OK)
      return;
    status_ = ZX_ERR_INVALID_ARGS;
    error_ = error;
  }

  WireFormatVersion wire_format() { return wire_format_version_; }

  size_t CurrentLength() const { return next_out_of_line_; }

#ifdef __Fuchsia__
  size_t CurrentHandleCount() const { return handle_index_; }
#else
  constexpr size_t CurrentHandleCount() const { return 0; }
#endif

  zx_status_t status() { return status_; }
  const char* error() { return error_; }

 private:
  fidl::IncomingMessage body_;

  // The body_offset_ is either 16 (when decoding the body of a transactional message, which is
  // itself a concatenation of 2 FIDL messages, the header and body), or 0 (when decoding a
  // standalone "at-rest" message body).
  uint32_t body_offset_ = 0;
#ifdef __Fuchsia__
  uint32_t handle_index_ = 0;
#endif

  size_t next_out_of_line_ = 0;

  WireFormatVersion wire_format_version_;

  zx_status_t status_ = ZX_OK;
  const char* error_ = nullptr;
};

}  // namespace fidl::internal

#endif  // SRC_LIB_FIDL_CPP_INCLUDE_LIB_FIDL_CPP_NATURAL_DECODER_H_
