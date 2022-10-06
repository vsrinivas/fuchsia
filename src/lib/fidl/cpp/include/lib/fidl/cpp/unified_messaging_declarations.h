// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_FIDL_CPP_INCLUDE_LIB_FIDL_CPP_UNIFIED_MESSAGING_DECLARATIONS_H_
#define SRC_LIB_FIDL_CPP_INCLUDE_LIB_FIDL_CPP_UNIFIED_MESSAGING_DECLARATIONS_H_

#include <lib/fidl/cpp/wire/wire_messaging_declarations.h>

#include <cstdint>

// This header centralizes the forward declarations for the various types in the
// unified messaging layer. The C++ FIDL code generator populates the concrete
// definitions from FIDL protocols in the schema.
namespace fidl {

// |Request| represents the request of a FIDL method call, using natural
// types. See |WireRequest| for the equivalent using wire types.
//
// When |Method| request has a payload, |Request| inherits from the payload
// type, exposing the operations of that type.
//
// When |Method| request has no payload, those operations will be absent.
//
// When |Method| has no request (event), this class will be undefined.
template <typename Method>
class Request;

// |Response| represents the response of a FIDL method call, using natural
// types. See |WireResponse| for the equivalent using wire types.
//
// When |Method| response has a payload, |Response| inherits from:
//
// - If |Method| uses the error syntax:
//     - If the success value is empty: `fit::result<AppError>`.
//     - Otherwise: `fit::result<AppError, SuccessValue>`.
// - If |Method| does not use the error syntax: the payload type.
//
// When |Method| response has no payload, those operations will be absent.
//
// When |Method| has no response (one-way), this class will be undefined.
template <typename Method>
class Response;

// |Event| represents an incoming FIDL event using natural types. See
// |WireEvent| for the equivalent using wire types.
//
// When |Method| event has a payload, |Event| inherits from:
//
// - If |Method| uses the error syntax:
//     - If the success value is empty: `fit::result<AppError>`.
//     - Otherwise: `fit::result<AppError, SuccessValue>`.
// - If |Method| does not use the error syntax: the payload type.
//
// When |Method| has no payload, those operations will be absent.
//
// When |Method| is not an event, this class will be undefined.
template <typename Method>
class Event;

// |Result| represents the result of calling a two-way FIDL method |Method|.
//
// It inherits from different `fit::result` types depending on |Method|:
//
// - When the method does not use the error syntax:
//     - When the method response has no body:
//
//           fit::result<fidl::Error>
//
//     - When the method response has a body:
//
//           fit::result<fidl::Error, MethodPayload>
//
//       where `fidl::Error` is a type representing any transport error or
//       protocol level terminal errors such as epitaphs, and |MethodPayload|
//       is the response type.
//
// - When the method uses the error syntax:
//     - When the method response payload is an empty struct:
//
//           fit::result<fidl::ErrorsIn<Method>>
//
//     - When the method response payload is not an empty struct:
//
//           fit::result<fidl::ErrorsIn<Method>, MethodPayload>
//
//       where |MethodPayload| is the success type.
//
// See also |fidl::ErrorsIn|.
template <typename Method>
class Result;

namespace internal {

// |MessageTraits| contains information about a request or response message:
// |Message| must be either a |fidl::Request<Foo>| or |fidl::Response<Foo>|.
//
// - bool kHasPayload: whether the message has a payload object. For example, a
//                     `Foo(struct {})` has a payload that is an empty struct.
// - Payload:          if |kHasPayload| is true, a type alias to the payload.
template <typename Message>
struct MessageTraits;

// |NaturalWeakEventSender| borrows the server endpoint from a binding object and
// exposes methods for sending events with natural types.
template <typename FidlProtocol>
class NaturalWeakEventSender;

// |NaturalEventSender| borrows a server endpoint and exposes methods for sending
// events with natural types.
template <typename FidlProtocol>
class NaturalEventSender;

// |NaturalSyncClientImpl| implements methods for making synchronous
// FIDL calls with natural types.
//
// All specializations of |NaturalSyncClientImpl| should inherit from
// |fidl::internal::SyncEndpointManagedVeneer|.
template <typename Protocol>
class NaturalSyncClientImpl;

// |NaturalClientImpl| implements methods for making asynchronous FIDL calls
// with natural types.
//
// All specializations of |NaturalClientImpl| should inherit from
// |fidl::internal::NaturalClientBase|.
template <typename Protocol>
class NaturalClientImpl;

// |NaturalMethodTypes| gives access to:
// - |Completer|: the completer type associated with a particular method.
// - if two-way:
//     - |ResultCallback|: the client callback taking a |fidl::Result| type.
//     - |IsAbsentBody|: whether the response has no body.
//     - |kHasDomainError|: whether the method uses the error syntax.
//     - if using the error syntax:
//         - |IsEmptyStructPayload|: whether the success payload is an empty struct.
template <typename FidlMethod>
struct NaturalMethodTypes;

// |NaturalEventHandlerInterface| contains handlers for each event inside
// the protocol |FidlProtocol|.
template <typename FidlProtocol>
class NaturalEventHandlerInterface;

template <typename FidlProtocol>
class NaturalEventDispatcher;

// |NaturalServerDispatcher| is a helper type that decodes an incoming message
// and invokes the corresponding handler in the server implementation.
template <typename FidlProtocol>
struct NaturalServerDispatcher;

template <typename FidlMethod>
class NaturalCompleterBase;

}  // namespace internal

// |SyncEventHandler| is used by synchronous clients to handle events using
// natural types.
template <typename Protocol>
class SyncEventHandler;

// |AsyncEventHandler| is used by asynchronous clients to handle events using
// natural types. It also adds a callback for handling fatal errors.
template <typename Protocol>
class AsyncEventHandler;

// |Server| is a pure-virtual interface to be implemented by a server, receiving
// natural types.
template <typename Protocol>
class Server;

}  // namespace fidl

#endif  // SRC_LIB_FIDL_CPP_INCLUDE_LIB_FIDL_CPP_UNIFIED_MESSAGING_DECLARATIONS_H_
