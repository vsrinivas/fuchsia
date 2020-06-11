// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_LLCPP_LINEARIZED_AND_ENCODED_H_
#define LIB_FIDL_LLCPP_LINEARIZED_AND_ENCODED_H_

#include <lib/fidl/llcpp/coding.h>
#include <lib/fidl/llcpp/message_storage.h>

namespace fidl {
namespace internal {

template <typename FidlType>
using LinearizeBuffer = fidl::internal::ByteStorage<
    ::fidl::internal::ClampedMessageSize<FidlType, ::fidl::MessageDirection::kSending>()>;

template <bool>
class AlreadyLinearized {};

template <typename, typename>
class LinearizedAndEncodedImpl {};

// Implementation of LinearizedAndEncoded when the input is already linearized.
// Only encode will be performed because the object is already linearized.
template <typename FidlType>
class LinearizedAndEncodedImpl<FidlType, AlreadyLinearized<true>> final {
 public:
  explicit LinearizedAndEncodedImpl(FidlType* obj) {
    fidl::DecodedMessage<FidlType> decoded_msg(
        fidl::BytePart(reinterpret_cast<uint8_t*>(const_cast<FidlType*>(obj)),
                       FidlAlign(sizeof(FidlType)), FidlAlign(sizeof(FidlType))));
    result_ = fidl::Encode(std::move(decoded_msg));
  }
  LinearizedAndEncodedImpl(LinearizedAndEncodedImpl&&) = delete;
  LinearizedAndEncodedImpl(const LinearizedAndEncodedImpl&) = delete;
  LinearizedAndEncodedImpl& operator=(LinearizedAndEncodedImpl&&) = delete;
  LinearizedAndEncodedImpl& operator=(const LinearizedAndEncodedImpl&) = delete;

  fidl::EncodeResult<FidlType>& result() { return result_; }

 private:
  fidl::EncodeResult<FidlType> result_;
};

// Implementation of LinearizedAndEncoded when the input is not already linearized.
// Both linearization and encode will be performed.
template <typename FidlType>
class LinearizedAndEncodedImpl<FidlType, AlreadyLinearized<false>> final {
 public:
  explicit LinearizedAndEncodedImpl(FidlType* obj) {
    result_ = fidl::LinearizeAndEncode(obj, buf_.buffer());
  }
  LinearizedAndEncodedImpl(LinearizedAndEncodedImpl&&) = delete;
  LinearizedAndEncodedImpl(const LinearizedAndEncodedImpl&) = delete;
  LinearizedAndEncodedImpl& operator=(LinearizedAndEncodedImpl&&) = delete;
  LinearizedAndEncodedImpl& operator=(const LinearizedAndEncodedImpl&) = delete;

  fidl::EncodeResult<FidlType>& result() { return result_; }

 private:
  LinearizeBuffer<FidlType> buf_;
  fidl::EncodeResult<FidlType> result_;
};

// LinearizedAndEncoded produces a linearized and encoded version of the input object.
// - If the input is already linearized, this will encode the value.
// - If the input is not linearized, both linearization and encoding will be performed.
//   This means allocating a buffer for linearization.
template <typename FidlType>
using LinearizedAndEncoded =
    LinearizedAndEncodedImpl<FidlType, AlreadyLinearized<!FidlType::HasPointer>>;

}  // namespace internal
}  // namespace fidl

#endif  // LIB_FIDL_LLCPP_LINEARIZED_AND_ENCODED_H_
