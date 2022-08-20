// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_FIDL_CPP_INCLUDE_LIB_FIDL_CPP_NATURAL_DECODER_H_
#define SRC_LIB_FIDL_CPP_INCLUDE_LIB_FIDL_CPP_NATURAL_DECODER_H_

#include <lib/fidl/cpp/wire/message.h>
#include <zircon/fidl.h>

#ifdef __Fuchsia__
#include <lib/fidl/cpp/wire/internal/transport_channel.h>
#include <lib/zx/handle.h>
#include <lib/zx/object.h>
#else
#include <lib/fidl/cpp/wire/internal/transport_channel_host.h>
#endif

namespace fidl::internal {

class NaturalDecoder final {
 public:
  explicit NaturalDecoder(fidl::EncodedMessage message,
                          fidl::internal::WireFormatVersion wire_format_version);
  ~NaturalDecoder();

  template <typename T>
  T* GetPtr(size_t offset) {
    ZX_DEBUG_ASSERT(offset <= body_.bytes().size());
    return reinterpret_cast<T*>(body_.bytes().data() + offset);
  }

  size_t GetOffset(const void* ptr) const { return GetOffset(reinterpret_cast<uintptr_t>(ptr)); }
  size_t GetOffset(uintptr_t ptr) const {
    // The |ptr| value comes from the message buffer, which we've already
    // validated. That means it should correspond to a valid offset within the
    // message.
    size_t offset = ptr - reinterpret_cast<uintptr_t>(body_.bytes().data());
    ZX_DEBUG_ASSERT(offset <= body_.bytes().size());
    return offset;
  }

  [[nodiscard]] bool Alloc(size_t size, size_t* offset) {
    if (size > std::numeric_limits<uint32_t>::max()) {
      SetError(kCodingErrorAllocationSizeExceeds32Bits);
      return false;
    }
    size_t old = next_out_of_line_;
    size_t next_unaligned = next_out_of_line_ + size;
    size_t next = FIDL_ALIGN(next_unaligned);
    if (next > body_.bytes().size()) {
      SetError(kCodingErrorBackingBufferSizeExceeded);
      return false;
    }

    uint64_t padding;
    switch (next - next_unaligned) {
      case 0:
        padding = 0x0000000000000000;
        break;
      case 1:
        padding = 0xff00000000000000;
        break;
      case 2:
        padding = 0xffff000000000000;
        break;
      case 3:
        padding = 0xffffff0000000000;
        break;
      case 4:
        padding = 0xffffffff00000000;
        break;
      case 5:
        padding = 0xffffffffff000000;
        break;
      case 6:
        padding = 0xffffffffffff0000;
        break;
      case 7:
        padding = 0xffffffffffffff00;
        break;
      default:
        __builtin_unreachable();
    }
    if (*GetPtr<uint64_t>(next - 8) & padding) {
      SetError(kCodingErrorInvalidPaddingBytes);
      return false;
    }

    next_out_of_line_ = next;
    *offset = old;
    return true;
  }

  void DecodeHandle(fidl_handle_t* value, HandleAttributes attr, size_t offset, bool is_optional) {
    zx_handle_t* handle = GetPtr<zx_handle_t>(offset);
    switch (*handle) {
      case FIDL_HANDLE_PRESENT: {
        if (handle_index_ >= body_.handle_actual()) {
          SetError(kCodingErrorTooManyHandlesConsumed);
          return;
        }

        zx_handle_t& body_handle = body_.handles()[handle_index_];

        if (body_.transport_vtable()->encoding_configuration->decode_process_handle) {
          const char* error;
          zx_status_t status =
              body_.transport_vtable()->encoding_configuration->decode_process_handle(
                  &body_handle, attr, handle_index_, body_.raw_handle_metadata(), &error);
          if (status != ZX_OK) {
            SetError(error);
            return;
          }
        }

        *value = body_handle;
        body_handle = FIDL_HANDLE_INVALID;
        ++handle_index_;
        return;
      }
      case FIDL_HANDLE_ABSENT: {
        if (is_optional) {
          *value = FIDL_HANDLE_INVALID;
        } else {
          SetError(kCodingErrorAbsentNonNullableHandle);
        }
        return;
      }
      default: {
        SetError(kCodingErrorInvalidPresenceIndicator);
        return;
      }
    }
  }

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

  size_t CurrentHandleCount() const { return handle_index_; }

  zx_status_t status() { return status_; }
  const char* error() { return error_; }

 private:
  fidl::EncodedMessage body_;

  uint32_t handle_index_ = 0;
  size_t next_out_of_line_ = 0;
  WireFormatVersion wire_format_version_;

  zx_status_t status_ = ZX_OK;
  const char* error_ = nullptr;
};

}  // namespace fidl::internal

#endif  // SRC_LIB_FIDL_CPP_INCLUDE_LIB_FIDL_CPP_NATURAL_DECODER_H_
