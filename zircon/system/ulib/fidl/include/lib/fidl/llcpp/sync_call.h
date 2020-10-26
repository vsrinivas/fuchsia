// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_LLCPP_SYNC_CALL_H_
#define LIB_FIDL_LLCPP_SYNC_CALL_H_

#include <lib/fidl/llcpp/coding.h>
#include <lib/fidl/llcpp/message_storage.h>
#include <lib/fidl/llcpp/traits.h>

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <memory>

namespace fidl {

// Calculates the maximum possible message size for a FIDL type,
// clamped at the Zircon channel packet size.
// TODO(fxbug.dev/8093): Always request the message context.
template <typename FidlType, const MessageDirection Direction = MessageDirection::kReceiving>
constexpr uint32_t MaxSizeInChannel() {
  return internal::ClampedMessageSize<FidlType, Direction>();
}

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
