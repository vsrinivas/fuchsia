// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_FIDL_CPP_INCLUDE_LIB_FIDL_CPP_UNIFIED_MESSAGING_H_
#define SRC_LIB_FIDL_CPP_INCLUDE_LIB_FIDL_CPP_UNIFIED_MESSAGING_H_

#include <lib/fidl/cpp/encoder.h>
#include <lib/fidl/cpp/internal/message_extensions.h>
#include <lib/fidl/cpp/internal/natural_client_base.h>
#include <lib/fidl/cpp/internal/natural_types.h>
#include <lib/fidl/cpp/natural_types.h>
#include <lib/fidl/cpp/unified_messaging_declarations.h>
#include <lib/fidl/llcpp/message.h>
#include <lib/fidl/llcpp/transaction.h>
#include <lib/fidl/llcpp/wire_messaging.h>
#include <lib/fitx/result.h>

#include <cstdint>

namespace fidl {

namespace internal {

template <typename FidlMethod>
struct MethodTypes {
  using Completer = ::fidl::Completer<>;
};

template <typename FidlMethod>
using NaturalCompleter = typename fidl::internal::MethodTypes<FidlMethod>::Completer;

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

    if constexpr (Traits::kHasPayload) {
      // Delegate into the decode logic of the payload.
      const fidl_message_header& header = *message.header();
      auto metadata = ::fidl::internal::WireFormatMetadata::FromTransactionalHeader(header);
      ::fitx::result decode_result = DecodeFrom<typename Traits::Payload>(
          ::fidl::internal::SkipTransactionHeader(std::move(message)), metadata);
      if (decode_result.is_error()) {
        return decode_result.take_error();
      }
      return ::fitx::ok(Message{std::move(decode_result.value())});
    }

    return ::fitx::ok(Message{});
  }
};

// Encode |payload| as part of a request/response message without validating.
//
// |encoder| must be initialized with a transactional header with the
// appropriate method ordinal.
//
// To reducing branching in generated code, |payload| may be |std::nullopt|, in
// which case the message will be encoded without a payload (header-only
// messages).
//
// Implementation notes: validation is currently performed later in
// |NaturalClientMessenger| because this mirrors the existing flow in HLCPP,
// simplifying initial work. TODO(fxbug.dev/82189): As part of designing the
// encoding/decoding of natural objects, we should decouple them from
// |fidl::HLCPPOutgoingMessage| and perform validation as part of encoding.
// This helper function may then be removed, since we could use the public
// encoding API of the domain object instead of |EncodeWithoutValidating|.
template <typename Payload = const cpp17::nullopt_t&>
::fidl::HLCPPOutgoingMessage EncodeTransactionalMessageWithoutValidating(
    ::fidl::internal::NaturalMessageEncoder<fidl::internal::ChannelTransport>& encoder,
    Payload&& payload = cpp17::nullopt) {
  // When the caller omits the |payload| argument, it will default to
  // |cpp17::nullopt|, which is of type |cpp17::nullopt_t|.
  constexpr bool kHasPayload = !std::is_same_v<cpp20::remove_cvref_t<Payload>, cpp17::nullopt_t>;
  if constexpr (kHasPayload) {
    encoder.Alloc(::fidl::internal::NaturalEncodingInlineSize<Payload>(&encoder));
    ::fidl::internal::NaturalCodingTraits<Payload>::Encode(&encoder, &payload,
                                                           sizeof(fidl_message_header_t));
  }

  return encoder.GetMessage();
}

inline ::fitx::result<::fidl::Error> ToFitxResult(::fidl::Result result) {
  if (result.ok()) {
    return ::fitx::ok();
  }
  return ::fitx::error<::fidl::Error>(result);
}

}  // namespace internal

// |ClientCallback| is the async callback type used in the |fidl::Client| for
// the FIDL method |Method| that propagates errors, that works with natural
// domain objects.
//
// It is of the form:
//
//     void Callback(::fitx::result<::fidl::Error, ::fidl::Response<Method>>&);
//
template <typename Method>
using ClientCallback = typename internal::ClientCallbackTraits<Method>::ResultCallback;

// |ClientResponseCallback| is the async callback type used in the
// |fidl::Client| for the FIDL method |Method| that ignores errors, that works
// with natural domain objects.
//
// It is of the form:
//
//     void Callback(::fidl::Response<Method>&);
//
template <typename Method>
using ClientResponseCallback = typename internal::ClientCallbackTraits<Method>::ResponseCallback;

}  // namespace fidl

#endif  // SRC_LIB_FIDL_CPP_INCLUDE_LIB_FIDL_CPP_UNIFIED_MESSAGING_H_
