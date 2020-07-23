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

// The base class for response-owning and non-owning SyncCalls.
//
// It is meant to support |OwnedSyncCallBase| and |UnownedSyncCallBase|.
template <typename ResponseType>
class SyncCallBase : private StatusAndError {
 public:
  SyncCallBase(const SyncCallBase&) = delete;
  SyncCallBase& operator=(const SyncCallBase&) = delete;

  SyncCallBase(StatusAndError&& other) : StatusAndError(std::move(other)) {}

  using StatusAndError::error;
  using StatusAndError::ok;
  using StatusAndError::SetStatus;
  using StatusAndError::status;

  // Convenience accessor for the FIDL response message pointer.
  // The returned pointer is never null, unless the object is moved.
  // Asserts that the call was successful.
  ResponseType* Unwrap() {
    ZX_ASSERT(status_ == ZX_OK);
    return message_.message();
  }

  // Convenience accessor for the FIDL response message pointer.
  // Asserts that the object was not moved.
  // Asserts that the call was successful.
  ResponseType& value() {
    ZX_ASSERT(status_ == ZX_OK);
    ZX_ASSERT(message_.message() != nullptr);
    return *message_.message();
  }
  const ResponseType& value() const {
    ZX_ASSERT(status_ == ZX_OK);
    ZX_ASSERT(message_.message() != nullptr);
    return *message_.message();
  }

  // Convenience accessor for the FIDL response message pointer.
  // Asserts that the object was not moved.
  // Asserts that the call was successful.
  ResponseType* operator->() { return &value(); }
  const ResponseType* operator->() const { return &value(); }

  // Convenience accessor for the FIDL response message pointer.
  // Asserts that the object was not moved.
  // Asserts that the call was successful.
  ResponseType& operator*() { return value(); }
  const ResponseType& operator*() const { return value(); }

  BytePart& bytes() { return message_.bytes(); }

 protected:
  SyncCallBase() = default;
  ~SyncCallBase() = default;

  SyncCallBase(SyncCallBase&& other) = default;
  SyncCallBase& operator=(SyncCallBase&& other) = default;

  using StatusAndError::error_;
  using StatusAndError::SetFailure;
  using StatusAndError::status_;

  // Initialize ourself from the DecodeResult corresponding to the response.
  void SetResult(fidl::DecodeResult<ResponseType> decode_result) {
    StatusAndError::status_ = decode_result.status;
    StatusAndError::error_ = decode_result.error;
    message_ = std::move(decode_result.message);
    ZX_ASSERT(StatusAndError::status_ != ZX_OK || message_.is_valid());
  }

  fidl::DecodedMessage<ResponseType>& decoded_message() { return message_; }

 private:
  fidl::DecodedMessage<ResponseType> message_;
};

// Class representing the result of a two-way FIDL call, without ownership of the response buffers.
// It is always inherited by generated code performing the call. Do not instantiate manually.
// Type returned by the caller-allocating flavor will inherit from this class.
//
// Holds a |DecodedMessage<ResponseType>| in addition to providing status() and error().
// If status() is ZX_OK, Unwrap() returns a valid decoded message of type ResponseType.
// If status() is ZX_OK, value() returns a valid decoded message of type ResponseType.
// Otherwise, error() contains a human-readable string for debugging purposes.
template <typename ResponseType>
using UnownedSyncCallBase = SyncCallBase<ResponseType>;

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
