// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_LLCPP_SYNC_CALL_H_
#define LIB_FIDL_LLCPP_SYNC_CALL_H_

#include <lib/fidl/llcpp/coding.h>
#include <lib/fidl/llcpp/decoded_message.h>
#include <lib/fidl/llcpp/encoded_message.h>
#include <lib/fidl/llcpp/message_storage.h>
#include <lib/fidl/llcpp/traits.h>

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <memory>

namespace fidl {
namespace internal {

// Class representing the result of a one-way FIDL call.
// status() returns the encoding and transport level status.
// If status() is not ZX_OK, error() contains a human-readable string for debugging purposes.
class StatusAndError : public FromFailureMixin<StatusAndError> {
 public:
  StatusAndError() = default;
  StatusAndError(zx_status_t status, const char* error) : status_(status), error_(error) {
    ZX_DEBUG_ASSERT(!(status_ == ZX_OK && error_));
  }

  StatusAndError(const StatusAndError&) = delete;
  StatusAndError& operator=(const StatusAndError&) = delete;

  StatusAndError(StatusAndError&&) = default;
  StatusAndError& operator=(StatusAndError&&) = default;

  template <typename SomeResult>
  StatusAndError(SomeResult failure) : StatusAndError(failure.status, failure.error) {
    ZX_DEBUG_ASSERT(status_ != ZX_OK);
  }

  [[nodiscard]] zx_status_t status() const { return status_; }
  [[nodiscard]] const char* error() const { return error_; }
  [[nodiscard]] bool ok() const { return status_ == ZX_OK; }

 protected:
  // Initialize ourself from one of EncodeResult, DecodeResult, LinearizeResult, in the case of
  // error hence there is no message.
  template <typename SomeResult>
  void SetFailure(SomeResult failure) {
    ZX_DEBUG_ASSERT(failure.status != ZX_OK);
    status_ = failure.status;
    error_ = failure.error;
  }

  void SetStatus(zx_status_t status, const char* error) {
    status_ = status;
    error_ = error;
  }

  zx_status_t status_ = ZX_ERR_INTERNAL;
  const char* error_ = nullptr;
};

}  // namespace internal

using internal::StatusAndError;

// An buffer holding data inline, sized specifically for |FidlType|.
// It can be used to allocate request/response buffers when using the caller-allocate or in-place
// flavor. For example:
//
//     fidl::Buffer<mylib::FooRequest> request_buffer;
//     fidl::Buffer<mylib::FooResponse> response_buffer;
//     auto result = mylib::Call::Foo(channel, request_buffer.view(), args, response_buffer.view());
//
// Since the |Buffer| type is always used at client side, we can assume responses are processed in
// the |kSending| context, and requests are processed in the |kReceiving| context.
template <typename FidlType>
using Buffer =
    internal::AlignedBuffer<internal::IsResponseType<FidlType>::value
                                ? MaxSizeInChannel<FidlType, MessageDirection::kReceiving>()
                                : MaxSizeInChannel<FidlType, MessageDirection::kSending>()>;

}  // namespace fidl

#endif  // LIB_FIDL_LLCPP_SYNC_CALL_H_
