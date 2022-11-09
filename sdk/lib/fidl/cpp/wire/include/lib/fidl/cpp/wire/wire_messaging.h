// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_CPP_WIRE_INCLUDE_LIB_FIDL_CPP_WIRE_WIRE_MESSAGING_H_
#define LIB_FIDL_CPP_WIRE_INCLUDE_LIB_FIDL_CPP_WIRE_WIRE_MESSAGING_H_

#include <lib/fidl/cpp/wire/base_wire_result.h>
#include <lib/fidl/cpp/wire/wire_messaging_declarations.h>
#include <lib/fit/function.h>

#include "lib/fidl/cpp/wire_format_metadata.h"

#ifdef __Fuchsia__
#include <lib/fidl/cpp/wire/internal/endpoints.h>
#include <lib/fidl/cpp/wire/message.h>
#include <lib/fidl/cpp/wire/soft_migration.h>
#include <lib/fidl/cpp/wire/transaction.h>
#include <zircon/fidl.h>
#endif  // __Fuchsia__

// # Wire messaging layer
//
// This header is the top-level #include for the zircon channel wire messaging layer.

namespace fidl {
#ifdef __Fuchsia__

template <typename FidlMethod>
using WireClientCallback =
    ::fit::callback<void(::fidl::internal::WireUnownedResultType<FidlMethod>&)>;

namespace internal {

// Default specialization for one-way completers.
template <typename FidlMethod>
struct WireMethodTypes {
  using Completer = fidl::Completer<>;
  static constexpr bool HasRequestPayload = false;
  using Request = void;
};

template <typename FidlMethod>
using WireCompleter = typename fidl::internal::WireMethodTypes<FidlMethod>::Completer;

template <typename FidlMethod>
using WireDomainError = typename fidl::internal::WireMethodTypes<FidlMethod>::DomainError;

template <typename FidlMethod>
using WireThenable = typename fidl::internal::WireMethodTypes<FidlMethod>::Thenable;

template <typename FidlMethod>
using WireBufferThenable = typename fidl::internal::WireMethodTypes<FidlMethod>::BufferThenable;

}  // namespace internal

template <typename FidlMethod>
using WireRequest = typename fidl::internal::WireMethodTypes<FidlMethod>::Request;

enum class DispatchResult;

// Dispatches the incoming message to one of the handlers functions in the protocol.
//
// This function should only be used in very low-level code, such as when manually
// dispatching a message to a server implementation.
//
// If there is no matching handler, it closes all the handles in |msg| and notifies
// |txn| of the error.
//
// Ownership of handles in |msg| are always transferred to the callee.
//
// The caller does not have to ensure |msg| has a |ZX_OK| status. It is idiomatic to pass a |msg|
// with potential errors; any error would be funneled through |InternalError| on the |txn|.
template <typename FidlProtocol>
void WireDispatch(fidl::WireServer<FidlProtocol>* impl, fidl::IncomingHeaderAndMessage&& msg,
                  fidl::Transaction* txn) {
  fidl::internal::WireServerDispatcher<FidlProtocol>::Dispatch(impl, std::move(msg), nullptr, txn);
}

// Attempts to dispatch the incoming message to a handler function in the server implementation.
//
// This function should only be used in very low-level code, such as when manually
// dispatching a message to a server implementation.
//
// If there is no matching handler, it returns |fidl::DispatchResult::kNotFound|, leaving the
// message and transaction intact. In all other cases, it consumes the message and returns
// |fidl::DispatchResult::kFound|. It is possible to chain multiple TryDispatch functions in this
// manner.
//
// The caller does not have to ensure |msg| has a |ZX_OK| status. It is idiomatic to pass a |msg|
// with potential errors; any error would be funneled through |InternalError| on the |txn|.
template <typename FidlProtocol>
fidl::DispatchResult WireTryDispatch(fidl::WireServer<FidlProtocol>* impl,
                                     fidl::IncomingHeaderAndMessage& msg, fidl::Transaction* txn) {
  FIDL_EMIT_STATIC_ASSERT_ERROR_FOR_TRY_DISPATCH(FidlProtocol);
  return fidl::internal::WireServerDispatcher<FidlProtocol>::TryDispatch(impl, msg, nullptr, txn);
}
#endif  // __Fuchsia__

namespace internal {

::fit::result<::fidl::Error> DecodeTransactionalMessageWithoutBody(
    ::fidl::IncomingHeaderAndMessage message);

::fit::result<::fidl::Error> DecodeTransactionalMessageWithoutBody(
    const ::fidl::EncodedMessage& message, ::fidl::WireFormatMetadata metadata);

// |InplaceDecodeTransactionalMessage| decodes a transactional incoming message
// to an instance of |Body| referencing some wire type.
//
// To reducing branching in generated code, |Body| may be |std::nullopt|, in
// which case the message will be decoded without a body (header-only
// messages), and the return type is `::fit::result<::fidl::Error>`. Otherwise,
// returns `::fit::result<::fidl::Error, ::fidl::DecodedValue<Body>>`.
//
// |message| is always consumed.
template <typename Body = std::nullopt_t>
auto InplaceDecodeTransactionalMessage(::fidl::IncomingHeaderAndMessage&& message)
    -> std::conditional_t<std::is_same_v<Body, std::nullopt_t>, ::fit::result<::fidl::Error>,
                          ::fit::result<::fidl::Error, fidl::DecodedValue<Body>>> {
  constexpr bool kHasBody = !std::is_same_v<Body, std::nullopt_t>;
  if constexpr (kHasBody) {
    if (!message.ok()) {
      return ::fit::error(message.error());
    }
    const fidl_message_header& header = *message.header();
    auto metadata = ::fidl::WireFormatMetadata::FromTransactionalHeader(header);
    fidl::EncodedMessage body_message = std::move(message).SkipTransactionHeader();
    // Delegate into the decode logic of the body.
    return ::fidl::InplaceDecode<Body>(std::move(body_message), metadata);
  } else {
    return DecodeTransactionalMessageWithoutBody(std::move(message));
  }
}

#ifdef __Fuchsia__

template <typename FidlMethod>
auto InplaceDecodeTransactionalResponse(::fidl::IncomingHeaderAndMessage&& message) {
  using Body = std::conditional_t<FidlMethod::kHasResponseBody, ::fidl::WireResponse<FidlMethod>,
                                  std::nullopt_t>;
  return ::fidl::internal::InplaceDecodeTransactionalMessage<Body>(std::move(message));
}

template <typename FidlMethod>
auto InplaceDecodeTransactionalRequest(::fidl::IncomingHeaderAndMessage&& message) {
  using Body = std::conditional_t<FidlMethod::kHasRequestBody,
                                  typename WireMethodTypes<FidlMethod>::Request, std::nullopt_t>;
  return ::fidl::internal::InplaceDecodeTransactionalMessage<Body>(std::move(message));
}

template <typename FidlMethod>
auto InplaceDecodeTransactionalEvent(::fidl::IncomingHeaderAndMessage&& message) {
  using Body = std::conditional_t<FidlMethod::kHasResponseBody, ::fidl::WireEvent<FidlMethod>,
                                  std::nullopt_t>;
  return ::fidl::internal::InplaceDecodeTransactionalMessage<Body>(std::move(message));
}

#endif  // __Fuchsia__

template <typename... T>
::fidl::Status StatusFromResult(const ::fit::result<::fidl::Error, T...>& r) {
  if (r.is_ok()) {
    return ::fidl::Status::Ok();
  }
  return r.error_value();
}

}  // namespace internal

}  // namespace fidl

#endif  // LIB_FIDL_CPP_WIRE_INCLUDE_LIB_FIDL_CPP_WIRE_WIRE_MESSAGING_H_
