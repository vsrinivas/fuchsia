// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_LLCPP_LINEARIZED_H_
#define LIB_FIDL_LLCPP_LINEARIZED_H_

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
class LinearizedImpl {};

// Implementation of Linearized when the input is already linearized.
// This is effectively a no-op - the input object is cast to bytes.
template <typename FidlType>
class LinearizedImpl<FidlType, AlreadyLinearized<true>> final {
 public:
  explicit LinearizedImpl(FidlType* obj) {
    result_ = fidl::LinearizeResult(ZX_OK, nullptr,
                                    fidl::DecodedMessage<FidlType>(fidl::BytePart(
                                        reinterpret_cast<uint8_t*>(const_cast<FidlType*>(obj)),
                                        FidlAlign(sizeof(FidlType)), FidlAlign(sizeof(FidlType)))));
  }
  LinearizedImpl(LinearizedImpl&&) = delete;
  LinearizedImpl(const LinearizedImpl&) = delete;
  LinearizedImpl& operator=(LinearizedImpl&&) = delete;
  LinearizedImpl& operator=(const LinearizedImpl&) = delete;

  fidl::LinearizeResult<FidlType>& result() { return result_; }

 private:
  fidl::LinearizeResult<FidlType> result_;
};

// Implementation of Linearized when the input is not already linearized.
// The input will be linearized into a buffer.
template <typename FidlType>
class LinearizedImpl<FidlType, AlreadyLinearized<false>> final {
 public:
  explicit LinearizedImpl(FidlType* obj) { result_ = fidl::Linearize(obj, buf_.buffer()); }
  LinearizedImpl(LinearizedImpl&&) = delete;
  LinearizedImpl(const LinearizedImpl&) = delete;
  LinearizedImpl& operator=(LinearizedImpl&&) = delete;
  LinearizedImpl& operator=(const LinearizedImpl&) = delete;

  fidl::LinearizeResult<FidlType>& result() { return result_; }

 private:
  LinearizeBuffer<FidlType> buf_;
  fidl::LinearizeResult<FidlType> result_;
};

// Linearized produces a linearized verson of the input object.
// - If the input is already linearized, this will cast the input to bytes and
// return it.
// - If the input is not already linearized, it will be linearized into a
// buffer.
// The resulting Linearized object must stay in scope while the
// LinearizedResult produced by the result() method is still being used.
template <typename FidlType>
using Linearized = LinearizedImpl<FidlType, AlreadyLinearized<!FidlType::HasPointer>>;

}  // namespace internal
}  // namespace fidl

#endif  // LIB_FIDL_LLCPP_LINEARIZED_H_
