// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_CPP_WIRE_INCLUDE_LIB_FIDL_CPP_WIRE_WIRE_MESSAGING_H_
#define LIB_FIDL_CPP_WIRE_INCLUDE_LIB_FIDL_CPP_WIRE_WIRE_MESSAGING_H_

#include <lib/fidl/cpp/wire/base_wire_result.h>
#include <lib/fidl/cpp/wire/wire_messaging_declarations.h>
#include <lib/fit/function.h>

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
using WireApplicationError = typename fidl::internal::WireMethodTypes<FidlMethod>::ApplicationError;

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

// Temporary transitional macro for the removal of empty request views from server implementations.
// TODO(ianloic): remove as part of https://fxbug.dev/108342 cleanup
#define FIDL_NO_EMPTY_SERVER_REQUEST_VIEWS

}  // namespace fidl

#endif  // LIB_FIDL_CPP_WIRE_INCLUDE_LIB_FIDL_CPP_WIRE_WIRE_MESSAGING_H_
