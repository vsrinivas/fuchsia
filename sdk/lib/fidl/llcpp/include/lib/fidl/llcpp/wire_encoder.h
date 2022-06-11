
// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_LLCPP_INCLUDE_LIB_FIDL_LLCPP_WIRE_ENCODER_H_
#define LIB_FIDL_LLCPP_INCLUDE_LIB_FIDL_LLCPP_WIRE_ENCODER_H_

#include <lib/fidl/llcpp/coding_errors.h>
#include <lib/fidl/llcpp/internal/transport.h>
#include <lib/fidl/llcpp/wire_coding_common.h>
#include <lib/fitx/result.h>

namespace fidl::internal {

class WireEncoder {
 public:
  explicit WireEncoder(const CodingConfig* coding_config, zx_channel_iovec_t* iovecs,
                       size_t iovec_capacity, fidl_handle_t* handles,
                       fidl_handle_metadata_t* handle_metadata, size_t handle_capacity,
                       uint8_t* backing_buffer, size_t backing_buffer_capacity);

  // Alloc allocates a new buffer.
  // It has been optimized to be small in size so that it can be inlined.
  __ALWAYS_INLINE bool Alloc(size_t size, WirePosition* position) {
    // For inline types, the value is non-zero and bounded (<= uint32_t max).
    // For vector / string / table it is necessary to check both bounds
    // (done outside of this function in order to prevent unnecessary checks.)
    ZX_DEBUG_ASSERT(size != 0);
    ZX_DEBUG_ASSERT(size <= std::numeric_limits<uint32_t>::max());

    *position = WirePosition(current_iovec_bytes_);
    current_iovec_bytes_ += FIDL_ALIGN_64(size);
    if (likely(current_iovec_bytes_ <= current_iovec_bytes_end_)) {
      *reinterpret_cast<uint64_t*>(current_iovec_bytes_ - 8) = 0;
      return true;
    }
    // Errors are detected in |Finish| by observing that
    // current_iovec_bytes_ > current_iovec_bytes_end_.
    // The motivation for this is to reduce binary size by not using additional instructions
    // to set the error here.
    return false;
  }

  // Use iovec optimizations to encode memcpyable vector bodies.
  void EncodeMemcpyableVector(const void* data, size_t count, size_t stride) {
    size_t size = count * stride;
    if (unlikely(iovec_actual_ + 2 >= iovec_capacity_)) {
      // There aren't enough iovecs available. Copy into the existing iovec.
      // There must be at least one iovec for the pointed buffer and one for
      // the remaining content to point to an iovec.
      WirePosition position;
      if (unlikely(!Alloc(size, &position))) {
        return;
      }
      memcpy(position.As<void>(), data, size);
      return;
    }

    // Normally, FIDL "aligns up" up to the nearest 8 byte value.
    // Here, we align down to only reference an aligned region of the iovec.
    size_t aligned_down = size & ~7ull;
    if (likely(aligned_down > 0)) {
      size_t consumed_capacity = ConsumedBytesInCurrentIovec();
      // Note: consumed_capacity may be 0 for instance if there are multiple
      // byte vectors in a struct.
      if (likely(consumed_capacity > 0)) {
        iovecs_[iovec_actual_] = {
            .buffer = current_iovec_bytes_begin_,
            .capacity = static_cast<uint32_t>(consumed_capacity),
        };
        iovec_actual_++;
        byte_actual_ += consumed_capacity;
        current_iovec_bytes_begin_ = current_iovec_bytes_;
        current_iovec_bytes_ = current_iovec_bytes_begin_;
      }

      iovecs_[iovec_actual_] = {
          .buffer = data,
          .capacity = static_cast<uint32_t>(aligned_down),
      };
      iovec_actual_++;
      byte_actual_ += aligned_down;
    }

    // Note: the old coding-table based encoder left an unaligned size in the iovec
    // and referenced padding bytes from the next backing_buffer block.
    // Memcpying avoids alignment issues for copying to the kernel, but it isn't
    // clear that this is better - the extra memcpy carries overhead.
    // Consider re-evaluating.
    size_t unaligned_portion = size - aligned_down;
    if (unaligned_portion > 0) {
      WirePosition position;
      if (unlikely(!Alloc(unaligned_portion, &position))) {
        return;
      }
      memcpy(position.As<void>(), static_cast<const uint8_t*>(data) + aligned_down,
             unaligned_portion);
    }
  }

  void EncodeHandle(fidl_handle_t handle, HandleAttributes attr, WirePosition position,
                    bool is_optional);

  size_t CurrentLength() const { return byte_actual_ + ConsumedBytesInCurrentIovec(); }
  size_t CurrentHandleCount() const { return handle_actual_; }
  void SetError(const char* error) {
    // Only set error_ rather than both error_ and error_status_.
    // This allows SetError to be cheaply inlined and reduces generated code.
    if (error_ != nullptr) {
      return;
    }
    error_ = error;
  }
  void SetError(zx_status_t status, const char* error) {
    if (error_ != nullptr) {
      return;
    }
    error_status_ = status;
    error_ = error;
  }
  bool HasError() const { return error_ != nullptr || AvailableBytesInCurrentIovec() < 0; }

  struct Result {
    size_t iovec_actual;
    size_t handle_actual;
  };

  fitx::result<fidl::Error, Result> Finish() {
    if (unlikely(AvailableBytesInCurrentIovec() < 0)) {
      // |Alloc| deferred error checking. Report an error if the buffer size was exceeded.
      SetError(ZX_ERR_BUFFER_TOO_SMALL, kCodingErrorBackingBufferSizeExceeded);
    }
    if (unlikely(error_ != nullptr)) {
      coding_config_->close_many(handles_, handle_actual_);
      return fitx::error(fidl::Status::EncodeError(error_status_, error_));
    }
    if (likely(ConsumedBytesInCurrentIovec() > 0)) {
      ZX_DEBUG_ASSERT(iovec_actual_ < iovec_capacity_);
      iovecs_[iovec_actual_] = {
          .buffer = current_iovec_bytes_begin_,
          .capacity = static_cast<uint32_t>(ConsumedBytesInCurrentIovec()),
      };
      iovec_actual_++;
    }
    return fitx::ok(Result{
        .iovec_actual = iovec_actual_,
        .handle_actual = handle_actual_,
    });
  }

 private:
  int64_t AvailableBytesInCurrentIovec() const {
    return static_cast<int64_t>(current_iovec_bytes_end_ - current_iovec_bytes_);
  }
  int64_t ConsumedBytesInCurrentIovec() const {
    return static_cast<int64_t>(current_iovec_bytes_ - current_iovec_bytes_begin_);
  }

  const CodingConfig* coding_config_;
  zx_channel_iovec_t* iovecs_;
  size_t iovec_capacity_;
  size_t iovec_actual_ = 0;

  fidl_handle_t* handles_;
  fidl_handle_metadata_t* handle_metadata_;
  size_t handle_capacity_;
  size_t handle_actual_ = 0;

  uint8_t* current_iovec_bytes_;
  uint8_t* current_iovec_bytes_begin_;
  uint8_t* current_iovec_bytes_end_;

  // All bytes across all iovecs.
  size_t byte_actual_ = 0;

  // An error is considered to be set if |error_| is not null.
  // The error_status_ value (whether set to a new value or not) is then used to determine
  // the error code.
  zx_status_t error_status_ = ZX_ERR_INVALID_ARGS;
  const char* error_ = nullptr;
};

}  // namespace fidl::internal

#endif  // LIB_FIDL_LLCPP_INCLUDE_LIB_FIDL_LLCPP_WIRE_ENCODER_H_
