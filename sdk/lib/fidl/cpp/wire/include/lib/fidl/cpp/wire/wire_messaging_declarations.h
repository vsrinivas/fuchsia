// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_CPP_WIRE_INCLUDE_LIB_FIDL_CPP_WIRE_WIRE_MESSAGING_DECLARATIONS_H_
#define LIB_FIDL_CPP_WIRE_INCLUDE_LIB_FIDL_CPP_WIRE_WIRE_MESSAGING_DECLARATIONS_H_

// This header contains forward definitions that support sending and receiving
// wire domain objects over Zircon channels for IPC. The code generator should
// populate the implementation by generating template specializations for each
// class over FIDL method/protocol markers.
//
// Note: a recurring pattern below is a pair of struct/using declaration:
//
//     template <typename T> struct FooTraits;
//     template <typename T> using Foo = typename FooTraits<T>::Foo;
//
// The extra |FooTraits| type is a workaround for C++ not having type alias
// partial specialization. The code generator would specialize |FooTraits|,
// and the using-declarations are helpers to pull out items from the struct.
namespace fidl {

template <typename FidlMethod>
struct WireResponse;

template <typename FidlMethod>
struct WireEvent;

namespace internal {

// |WireResultUnwrap| is a template which will be specialized by generated code
// to provide the type that should be returned by the |Unwrap|, |value|, and
// dereference operator accessors on |BaseWireResult|. The template is not
// available for methods with an empty return value and no error, since those do
// not provide |Unwrap| methods.
//
// The generated specialization will contain a single type alias called |Type|
// which is the type that will be used in the |Unwrap| and related methods of
// the |WireResult| and |WireUnownedResult|.
//
// For a method without error syntax, the |Type| will be the return type of the
// method specified in the FIDL file. For a method with error syntax, |Type|
// will be a |fit::result| of the error type specified in the FIDL file and a
// pointer to the return type (if the return type is not empty).
template <typename FidlMethod>
struct WireResultUnwrap;

template <typename FidlMethod>
struct TransactionalRequest;

template <typename FidlMethod>
struct TransactionalResponse;

template <typename FidlMethod>
struct TransactionalEvent;

}  // namespace internal

// |WireResultUnwrapType| is a template which aliases the type that should be
// returned by the |Unwrap|, |value|, and dereference operator accessors on
// |BaseWireResult|. The template is not available for methods with an empty
// return value and no error, since those do not provide |Unwrap| methods.
//
// For a method without error syntax, the |WireResultUnwrapType| will be the
// return type of the method specified in the FIDL file. For a method with error
// syntax, |WireResultUnwrapType| will be a |fit::result| of the error type
// specified in the FIDL file and a pointer to the return type (if the return
// type is not empty).
template <typename FidlMethod>
using WireResultUnwrapType = typename ::fidl::internal::WireResultUnwrap<FidlMethod>::Type;

#ifdef __Fuchsia__
// WireSyncEventHandler is used by synchronous clients to handle events for the
// given protocol.
template <typename FidlProtocol>
class WireSyncEventHandler;

// WireAsyncEventHandler is used by asynchronous clients to handle events and
// adds a callback for to report fatal errors.
template <typename FidlProtocol>
class WireAsyncEventHandler;

// WireServer is a pure-virtual interface to be implemented by a server.
// This interface uses typed channels (i.e. |fidl::ClientEnd<SomeProtocol>|
// and |fidl::ServerEnd<SomeProtocol>|).
template <typename FidlProtocol>
class WireServer;

// This is a temporary forward declaration to allow the //sdk/lib/sys/component
// service library to conditionally support natural servers when it is available.
// Intentional empty line to avoid language servers picking up this doc comment.
// TODO(fxbug.dev/92356): Remove this after the unified bindings are in the SDK.

template <typename FidlProtocol>
class Server;

template <typename FidlMethod>
class WireResponseContext;

template <typename FidlMethod>
class WireResult;

template <typename FidlMethod>
class WireUnownedResult;

#endif  // __Fuchsia__

namespace internal {

template <typename FidlMethod>
struct WireOrdinal;

// |IncomingMessageStorage| allocates the necessary storage for receiving a
// transactional message of type |FidlType| from its corresponding transport.
// The allocation may be inline if reasonably small. It is useful for allocating
// bespoke amount of memory when the code path is certain what is the message
// type, such as during synchronous calls.
//
// |FidlType| must be a specialization of |TransactionalResponse|.
//
// This class is shared between wire and natural types - both receive messages
// of the same shapes and then perform their own decoding.
template <typename FidlType>
struct IncomingMessageStorage;

// |IncomingMessageHandleStorage| allocates the necessary storage for receiving
// the handles for a transactional message of type |FidlType| from its
// corresponding transport. It is useful for allocating bespoke amount of memory
// when the code path is certain what is the message type, such as during
// synchronous calls.
//
// |FidlType| must be a specialization of |TransactionalResponse|.
//
// This class is shared between wire and natural types - both receive messages
// of the same shapes and then perform their own decoding.
template <typename FidlType>
struct IncomingMessageHandleStorage;

// |IncomingEventsStorage| allocates the necessary storage for receiving any one
// of the possible events defined in |FidlProtocol| from its corresponding
// transport. The allocation may be inline if reasonably small. It is useful for
// allocating bespoke amount of memory when the code path is certain what is the
// protocol type, such as during synchronous event handling.
//
// |FidlProtocol| must be a protocol marker type.
//
// This class is shared between wire and natural types - both receive messages
// of the same shapes and then perform their own decoding.
template <typename FidlProtocol>
struct IncomingEventsStorage;

// |IncomingEventsHandleStorage| allocates the necessary storage for receiving
// the handles for any one of the possible events defined in |FidlProtocol| from
// its corresponding transport. It is useful for allocating bespoke amount of
// memory when the code path is certain what is the protocol type, such as
// during synchronous event handling.
//
// |FidlProtocol| must be a protocol marker type.
//
// This class is shared between wire and natural types - both receive messages
// of the same shapes and then perform their own decoding.
template <typename FidlProtocol>
struct IncomingEventsHandleStorage;

#ifdef __Fuchsia__

// |WireWeakEventSender| borrows the server endpoint from a binding object and
// exposes methods for sending events using managed memory allocation.
template <typename FidlProtocol>
class WireWeakEventSender;

// |WireWeakBufferEventSender| borrows the server endpoint from a binding object and
// exposes methods for sending events using caller-controlled allocation.
template <typename FidlProtocol>
class WireWeakBufferEventSender;

// |WireEventSender| borrows a server endpoint and exposes methods for sending
// events using managed memory allocation.
template <typename FidlProtocol>
class WireEventSender;

// |WireBufferEventSender| borrows a server endpoint and exposes methods for
// sending events using caller-controlled allocation.
template <typename FidlProtocol>
class WireBufferEventSender;

// |WireWeakAsyncClientImpl| implements one-way FIDL calls with managed buffers.
// It borrows the transport through a weak reference when making calls.
template <typename FidlProtocol>
class WireWeakOnewayClientImpl;

// |WireWeakAsyncClientImpl| implements asynchronous FIDL calls with managed
// buffers. It borrows the transport through a weak reference when making calls.
template <typename FidlProtocol>
class WireWeakAsyncClientImpl;

// |WireWeakOnewayBufferClientImpl| implements one-way FIDL calls with
// caller-provided buffers. It borrows the transport through a weak reference
// when making calls.
template <typename FidlProtocol>
class WireWeakOnewayBufferClientImpl;

// |WireWeakAsyncBufferClientImpl| implements asynchronous FIDL calls with
// caller-provided buffers. It borrows the transport through a weak reference
// when making calls.
template <typename FidlProtocol>
class WireWeakAsyncBufferClientImpl;

// |WireSyncClientImpl| implements synchronous FIDL calls with managed buffers.
// It contains an unowned transport handle.
//
// TODO(fxbug.dev/78906): Consider merging this implementation with
// |WireWeakSyncClientImpl| to support thread-safe teardown of
// |fidl::WireSyncClient|s.
template <typename FidlProtocol>
class WireSyncClientImpl;

// |WireWeakSyncClientImpl| implements synchronous FIDL calls with managed
// buffers. It borrows the transport through a weak reference when making calls.
template <typename FidlProtocol>
class WireWeakSyncClientImpl;

// |WireSyncBufferClientImpl| implements synchronous FIDL calls with
// caller-provided buffers. It contains an unowned transport handle.
//
// TODO(fxbug.dev/78906): Consider merging this implementation with
// |WireWeakSyncBufferClientImpl| to support thread-safe teardown of
// |fidl::WireSyncClient|s.
template <typename FidlProtocol>
class WireSyncBufferClientImpl;

// |WireWeakSyncBufferClientImpl| implements synchronous FIDL calls with
// caller-provided buffers. It borrows the transport through a weak reference
// when making calls.
//
// TODO(fxbug.dev/85688): Generate this class.
template <typename FidlProtocol>
class WireWeakSyncBufferClientImpl;

template <typename FidlProtocol>
class WireEventHandlerInterface;

template <typename FidlProtocol>
class WireEventDispatcher;

template <typename FidlProtocol>
struct WireServerDispatcher;

// |WireBufferCompleterImpl| implements FIDL replies with wire types using
// caller-provided buffers.
template <typename FidlMethod>
class WireBufferCompleterImpl;

// |WireCompleterImpl| implements FIDL replies with wire types using managed
// buffers.
template <typename FidlMethod>
class WireCompleterImpl;

// |WireCompleterBase| composes |WireBufferCompleterImpl| and |WireCompleterImpl|
// to provide the overall completer API.
template <typename FidlMethod>
class WireCompleterBase;

// |WireMethodTypes| gives access to:
// - the completer type associated with a particular method.
// - if two-way and uses the error syntax: the domain error type in the
//   error syntax.
// - if two-way: the thenable type associated with a particular method.
template <typename FidlMethod>
struct WireMethodTypes;

#endif  // __Fuchsia__

}  // namespace internal

namespace testing {

template <typename FidlProtocol>
class WireTestBase;

template <typename FidlProtocol>
class WireSyncEventHandlerTestBase;

}  // namespace testing

}  // namespace fidl

#endif  // LIB_FIDL_CPP_WIRE_INCLUDE_LIB_FIDL_CPP_WIRE_WIRE_MESSAGING_DECLARATIONS_H_
