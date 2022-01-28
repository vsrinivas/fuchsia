// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_FIDL_CPP_INCLUDE_LIB_FIDL_CPP_UNIFIED_MESSAGING_DECLARATIONS_H_
#define SRC_LIB_FIDL_CPP_INCLUDE_LIB_FIDL_CPP_UNIFIED_MESSAGING_DECLARATIONS_H_

#include <lib/fidl/llcpp/wire_messaging_declarations.h>

#include <cstdint>

// This header centralizes the forward declarations for the various types in the
// unified messaging layer. The C++ FIDL code generator populates the concrete
// definitions from FIDL protocols in the schema.
namespace fidl {

// |Request| represents the request of a FIDL method call, using natural
// types. See |WireRequest| for the equivalent using wire types.
//
// When |Method| request has a payload, |Request| will expose the following
// operators for the user to access the payload:
//
//     MethodRequest& operator*();
//     MethodRequest* operator->();
//
// When |Method| request has no payload, those operators will be absent.
//
// When |Method| has no request (event), this class will be undefined.
template <typename Method>
class Request;

// |Response| represents the response of a FIDL method call, using natural
// types. See |WireResponse| for the equivalent using wire types.
//
// When |Method| response has a payload, |Response| will expose the following
// operators for the user to access the payload:
//
//     MethodResponse& operator*();
//     MethodResponse* operator->();
//
// When |Method| response has no payload, those operators will be absent.
//
// When |Method| has no response (one-way), this class will be undefined.
template <typename Method>
class Response;

// |Event| represents an incoming FIDL event using natural types. See
// |WireEvent| for the equivalent using wire types.
//
// When |Method| has a payload, |Event| will expose the following operators for
// the user to access the payload:
//
//     EventPayload& operator*();
//     EventPayload* operator->();
//
// When |Method| has no payload, those operators will be absent.
template <typename Method>
class Event;

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

// |NaturalClientImpl| implements methods for making synchronous and
// asynchronous FIDL calls with natural types.
//
// All specializations of |NaturalClientImpl| should inherit from
// |fidl::internal::NaturalClientBase|.
template <typename Protocol>
class NaturalClientImpl;

// |ClientCallbackTraits| contains two nested definitions, that describe the
// async callback types used in the |fidl::Client| for the FIDL method |Method|,
// that works with natural domain objects:
//
// - |ResultCallback|: the callback taking a |fitx::result| type.
// - |ResponseCallback|: the callback taking a |fidl::Response| type.
template <typename Method>
class ClientCallbackTraits;

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

// |AsyncEventHandler| is used by asynchronous clients to handle events using
// natural types. It also adds a callback for handling errors.
template <typename Protocol>
class AsyncEventHandler;

// |Server| is a pure-virtual interface to be implemented by a server, receiving
// natural types.
template <typename Protocol>
class Server;

}  // namespace fidl

#endif  // SRC_LIB_FIDL_CPP_INCLUDE_LIB_FIDL_CPP_UNIFIED_MESSAGING_DECLARATIONS_H_
