// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_LLCPP_SYNC_CALL_H_
#define LIB_FIDL_LLCPP_SYNC_CALL_H_

#include <lib/fidl/llcpp/coding.h>
#include <lib/fidl/llcpp/decoded_message.h>
#include <lib/fidl/llcpp/encoded_message.h>
#include <lib/fidl/llcpp/response_storage.h>
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
  StatusAndError(zx_status_t status, const char* error) : status_(status), error_(error) {}

  StatusAndError(const StatusAndError&) = delete;
  StatusAndError& operator=(const StatusAndError&) = delete;

  [[nodiscard]] zx_status_t status() const { return status_; }
  [[nodiscard]] const char* error() const { return error_; }
  [[nodiscard]] bool ok() const { return status_ == ZX_OK; }

 protected:
  StatusAndError(StatusAndError&&) = default;
  StatusAndError& operator=(StatusAndError&&) = default;

  // Initialize ourself from one of EncodeResult, DecodeResult, LinearizeResult, in the case of
  // error hence there is no message.
  template <typename SomeResult>
  void SetFailure(SomeResult failure) {
    ZX_DEBUG_ASSERT(failure.status != ZX_OK);
    status_ = failure.status;
    error_ = failure.error;
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

  using StatusAndError::error;
  using StatusAndError::ok;
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

// Base class representing the result of a two-way FIDL call, with ownership of the response buffer.
// It is always inherited by generated code performing the call. Do not instantiate manually.
// Types returned by the managed flavor will inherit from this class.
//
// Holds a |DecodedMessage<ResponseType>| in addition to providing status() and error().
// If status() is ZX_OK, Unwrap() returns a valid decoded message of type ResponseType.
// If status() is ZX_OK, value() returns a valid decoded message of type ResponseType.
// Otherwise, error() contains a human-readable string for debugging purposes.
//
// Note: this class does not add new members on top of |SyncCallBase|.
template <typename ResponseType>
class OwnedSyncCallBase : private SyncCallBase<ResponseType> {
  using Super = SyncCallBase<ResponseType>;
  using ResponseStorageType = ResponseStorage<ResponseType>;

 public:
  OwnedSyncCallBase(const OwnedSyncCallBase&) = delete;
  OwnedSyncCallBase& operator=(const OwnedSyncCallBase&) = delete;

  OwnedSyncCallBase(OwnedSyncCallBase&& other) {
    if (this != &other) {
      MoveImpl(std::move(other));
    }
  }

  OwnedSyncCallBase& operator=(OwnedSyncCallBase&& other) {
    if (this != &other) {
      MoveImpl(std::move(other));
    }
    return *this;
  }

  using Super::error;
  using Super::ok;
  using Super::status;
  using Super::Unwrap;
  using Super::value;
  using Super::operator->;
  using Super::operator*;

 protected:
  OwnedSyncCallBase() = default;
  ~OwnedSyncCallBase() {
    // Before handing over to the member and super class destructor, release the ownership
    // decoded message has on the storage first, to prevent use-after-free.
    Super::decoded_message().Reset(fidl::BytePart());
  }

  fidl::BytePart response_buffer() { return response_storage_.buffer(); }

  using Super::SetFailure;

  // Initialize ourself from the DecodeResult corresponding to the response.
  // Invariant: the address of the response message must equal to that of our managed buffer.
  void SetResult(fidl::DecodeResult<ResponseType> decode_result) {
    ZX_ASSERT(decode_result.message.message() ==
                  reinterpret_cast<ResponseType*>(response_buffer().data()) ||
              !decode_result.message.is_valid());
    Super::SetResult(std::move(decode_result));
  }

 private:
  ResponseStorageType response_storage_;

  void MoveImpl(OwnedSyncCallBase&& other) {
    Super::status_ = other.Super::status_;
    Super::error_ = other.Super::error_;

    Super::decoded_message().Reset(fidl::BytePart());
    if constexpr (ResponseStorageType::kWillCopyBufferDuringMove) {
      // Use the linearizer to update pointers and move handles, in the case of
      // inlined response buffers.
      if (other.Super::decoded_message().is_valid()) {
        // If there are pointers, they need to be patched.
        // Otherwise, we get away with a memcpy.
        if constexpr (NeedsEncodeDecode<ResponseType>::value && ResponseType::MaxOutOfLine > 0) {
          auto result =
              fidl::Linearize(other.Super::decoded_message().message(), response_buffer());
          (void)other.Super::decoded_message().Release();
          ZX_DEBUG_ASSERT(result.status == ZX_OK);
          Super::decoded_message() = std::move(result.message);
        } else {
          auto other_bytes = other.Super::decoded_message().Release();
          response_storage_ = std::move(other.response_storage_);
          auto our_bytes = response_buffer();
          our_bytes.set_actual(other_bytes.actual());
          Super::decoded_message().Reset(std::move(our_bytes));
        }
      }
    } else {
      // If the response buffer is a unique_ptr, just move the unique_ptr.
      response_storage_ = std::move(other.response_storage_);
      Super::decoded_message() = std::move(other.Super::decoded_message());
    }
  }
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
