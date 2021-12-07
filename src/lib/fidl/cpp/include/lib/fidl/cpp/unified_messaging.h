// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_FIDL_CPP_INCLUDE_LIB_FIDL_CPP_UNIFIED_MESSAGING_H_
#define SRC_LIB_FIDL_CPP_INCLUDE_LIB_FIDL_CPP_UNIFIED_MESSAGING_H_

#include <lib/fidl/cpp/internal/message_extensions.h>
#include <lib/fidl/cpp/internal/natural_client_base.h>
#include <lib/fidl/cpp/internal/natural_types.h>
#include <lib/fidl/llcpp/message.h>
#include <lib/fidl/llcpp/wire_messaging.h>
#include <lib/fitx/result.h>

#include <cstdint>

// This header centralizes the forward declarations for the various types in the
// unified messaging layer. The C++ FIDL code generator populates the concrete
// definitions from FIDL protocols in the schema.
namespace fidl {

// |Response| represents the response of a FIDL method call or event, using
// natural types. See |WireResponse| for the equivalent using wire types.
//
// When |Method| response has a payload, |Response| will expose the following
// operators for the user to access the payload:
//
// MethodResponse& operator*();
// MethodResponse* operator->();
//
// When |Method| response has no payload, those operators will be absent.
//
// When |Method| has no response (one-way), this class will be undefined.
template <typename Method>
class Response;

namespace internal {

// |NaturalClientImpl| implements methods for making synchronous and
// asynchronous FIDL calls with natural types.
//
// All specializations of |NaturalClientImpl| should inherit from
// |fidl::internal::NaturalClientBase|.
// TODO(fxbug.dev/60240): Generate this interface.
template <typename Protocol>
class NaturalClientImpl;

// |MessageTraits| contains information about a request or response message:
// |Message| must be either a |fidl::Request<Foo>| or |fidl::Response<Foo>|.
//
// - bool kHasPayload: whether the message has a payload object. For example, a
//                     `Foo(struct {})` has a payload that is an empty struct.
// - Payload:          if |kHasPayload| is true, a type alias to the payload.
template <typename Message>
struct MessageTraits;

// |MessageBase| is a mixin with common functionalities for transactional
// message wrappers.
//
// |Message| is either a |fidl::Request<Foo>| or |fidl::Response<Foo>|.
template <typename Message>
class MessageBase {
 public:
  // |DecodeTransactional| decodes a transactional incoming message to a
  // |Message| containing natural types.
  //
  // |message| is always consumed.
  static ::fitx::result<Error, Message> DecodeTransactional(::fidl::IncomingMessage&& message) {
    ZX_DEBUG_ASSERT(message.is_transactional());
    using Traits = MessageTraits<Message>;

    if constexpr (!Traits::kHasPayload) {
      return ::fitx::ok(Message{});
    }

    // Delegate into the decode logic of the payload.
    const fidl_message_header& header = *message.header();
    auto metadata = ::fidl::internal::WireFormatMetadata::FromTransactionalHeader(header);
    ::fitx::result decode_result = Traits::Payload::DecodeFrom(
        ::fidl::internal::SkipTransactionHeader(std::move(message)), metadata);
    if (decode_result.is_error()) {
      return decode_result.take_error();
    }
    return ::fitx::ok(Message{std::move(decode_result.value())});
  }
};

}  // namespace internal

// |AsyncEventHandler| is used by asynchronous clients to handle events
// using natural types. It also adds a callback for handling errors.
// TODO(fxbug.dev/60240): Generate this interface.
template <typename Protocol>
class AsyncEventHandler;

}  // namespace fidl

#endif  // SRC_LIB_FIDL_CPP_INCLUDE_LIB_FIDL_CPP_UNIFIED_MESSAGING_H_
