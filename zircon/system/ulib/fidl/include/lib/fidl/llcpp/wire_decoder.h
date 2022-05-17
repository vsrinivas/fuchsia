// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_LLCPP_WIRE_DECODER_H_
#define LIB_FIDL_LLCPP_WIRE_DECODER_H_

#include <lib/fidl/llcpp/coding_errors.h>
#include <lib/fidl/llcpp/internal/transport.h>
#include <lib/fidl/llcpp/wire_coding_common.h>

namespace fidl::internal {

class WireDecoder final {
 public:
  explicit WireDecoder(const CodingConfig* coding_config, uint8_t* bytes, size_t num_bytes,
                       fidl_handle_t* handles, fidl_handle_metadata_t* handle_metadata,
                       size_t num_handles);
  ~WireDecoder();

  [[nodiscard]] bool Alloc(size_t size, WirePosition* position) {
    if (size == 0) {
      // While not standardized, the existing coding table C/C++ encoder expect zero-sized vectors
      // to have a valid data pointer. Therefore, for compatibility with the existing encoder when
      // re-encoding, it is necessary to output a pointer to the decode buffer.
      *position = WirePosition(&bytes_[next_out_of_line_]);
      return true;
    }
    size_t old = next_out_of_line_;
    size_t next_unaligned = next_out_of_line_ + size;
    size_t next = FIDL_ALIGN_64(next_unaligned);
    if (unlikely(next > num_bytes_)) {
      SetError(ZX_ERR_BUFFER_TOO_SMALL, kCodingErrorBackingBufferSizeExceeded);
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
    if (unlikely(*reinterpret_cast<uint64_t*>(&bytes_[next - 8]) & padding)) {
      SetError(kCodingErrorInvalidPaddingBytes);
      return false;
    }

    next_out_of_line_ = next;
    *position = WirePosition(&bytes_[old]);
    return true;
  }

  void CloseNextNHandles(size_t count) {
    if (unlikely(count > num_handles_ - handle_index_)) {
      SetError(kCodingErrorInvalidNumHandlesSpecifiedInEnvelope);
      return;
    }

    coding_config_->close_many(&handles_[handle_index_], count);
    size_t end = handle_index_ + count;
    for (; handle_index_ < end; handle_index_++) {
      handles_[handle_index_] = FIDL_HANDLE_INVALID;
    }
  }

  void DecodeHandle(WirePosition position, HandleAttributes attr, bool is_optional) {
    zx_handle_t* handle = position.As<zx_handle_t>();
    switch (*handle) {
      case FIDL_HANDLE_PRESENT: {
        if (unlikely(handle_index_ >= num_handles_)) {
          SetError(kCodingErrorTooManyHandlesConsumed);
          return;
        }

        zx_handle_t* body_handle = &handles_[handle_index_];
        if (unlikely(*body_handle == FIDL_HANDLE_INVALID)) {
          SetError(kCodingErrorInvalidHandleInInput);
          return;
        }

        if (coding_config_->decode_process_handle) {
          const char* error;
          zx_status_t status = coding_config_->decode_process_handle(
              body_handle, attr, handle_index_, handle_metadata_, &error);
          if (unlikely(status != ZX_OK)) {
            SetError(error);
            return;
          }
        }

        *handle = *body_handle;
        ++handle_index_;
        return;
      }
      case FIDL_HANDLE_ABSENT: {
        if (likely(!is_optional)) {
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

  size_t CurrentLength() const { return next_out_of_line_; }

  size_t CurrentHandleCount() const { return handle_index_; }

  void SetError(const char* error) {
    if (HasError()) {
      return;
    }
    error_ = error;
  }
  void SetError(zx_status_t status, const char* error) {
    if (HasError()) {
      return;
    }
    error_status_ = status;
    error_ = error;
  }
  bool HasError() const { return error_ != nullptr; }

  fidl::Status Finish() {
    if (HasError()) {
      coding_config_->close_many(handles_, num_handles_);
      return fidl::Status::DecodeError(error_status_, error_);
    }
    return fidl::Status::Ok();
  }

 private:
  const CodingConfig* coding_config_;
  uint8_t* bytes_;
  size_t num_bytes_;
  fidl_handle_t* handles_;
  fidl_handle_metadata_t* handle_metadata_;
  size_t num_handles_;

  uint32_t handle_index_ = 0;

  size_t next_out_of_line_ = 0;

  zx_status_t error_status_ = ZX_ERR_INVALID_ARGS;
  const char* error_ = nullptr;
};

}  // namespace fidl::internal

#endif  // LIB_FIDL_LLCPP_WIRE_DECODER_H_
