// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_LLCPP_SYNC_CALL_H_
#define LIB_FIDL_LLCPP_SYNC_CALL_H_

#include <lib/fidl/llcpp/async_binding.h>
#include <lib/fidl/llcpp/coding.h>
#include <lib/fidl/llcpp/internal/arrow.h>
#include <lib/fidl/llcpp/internal/endpoints.h>
#include <lib/fidl/llcpp/internal/server_details.h>
#include <lib/fidl/llcpp/message_storage.h>
#include <lib/fidl/llcpp/traits.h>

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <memory>

namespace fidl {

//
// The logic here for calculating buffer size needs to be kept in sync with the
// one defined in tools/fidl/lib/fidlgen_cpp/protocol.go
//

// Helper to calculate the maximum possible message size for a FIDL type,
// clamped at the Zircon channel packet size.
template <typename FidlType, const MessageDirection Direction>
constexpr uint32_t MaxSizeInChannel() {
  return internal::ClampedMessageSize<FidlType, Direction>();
}

// Helper to calculate a safe buffer size for use in caller-allocating flavors
// to call |Method| from a synchronous client, assuming the size of each message
// (request/response) is clamped at the Zircon channel packet size.
//
// |Method| is a method marker that looks like
// |fuchsia_mylib::SomeProtocol::SomeMethod|.
//
// This could be used as part of determining an optimum initial size for a FIDL
// arena or buffer span.
template <typename Method>
constexpr uint32_t SyncClientMethodBufferSizeInChannel() {
  // TODO(fxbug.dev/85843): We should be able optimize this to just the max of
  // the send/receive size, once Zircon channel calls guarantee that the
  // send/receive buffers can overlap.
  uint32_t size = 0;
  static_assert(IsFidlTransactionalMessage<internal::TransactionalRequest<Method>>::value,
                "|Method| must be a FIDL method marker type");
  size += MaxSizeInChannel<internal::TransactionalRequest<Method>, MessageDirection::kSending>();
  // If it's a two-way method, count the response as well.
  if constexpr (IsFidlTransactionalMessage<internal::TransactionalResponse<Method>>::value) {
    size +=
        MaxSizeInChannel<internal::TransactionalResponse<Method>, MessageDirection::kReceiving>();
  }
  return size;
}

// Helper to calculate a safe buffer size for use in caller-allocating flavors
// to call |Method| from an asynchronous client, assuming the size of each
// message (request) is clamped at the Zircon channel packet size.
//
// An asynchronous client handles responses asynchronously, and from one place
// in the event loop. Therefore, only the request portion of a two-way call need
// to be factored in buffer size calculations.
//
// |Method| is a method marker that looks like
// |fuchsia_mylib::SomeProtocol::SomeMethod|.
//
// This could be used as part of determining an optimum initial size for a FIDL
// arena or buffer span.
template <typename Method>
constexpr uint32_t AsyncClientMethodBufferSizeInChannel() {
  static_assert(IsFidlTransactionalMessage<internal::TransactionalRequest<Method>>::value,
                "|Method| must be a FIDL method marker type");
  return MaxSizeInChannel<internal::TransactionalRequest<Method>, MessageDirection::kSending>();
}

// Helper to calculate a safe buffer size for use in caller-allocating flavors
// to reply to |Method| from a server, assuming the size of each message is
// clamped at the Zircon channel packet size.
//
// |Method| is a method marker that looks like
// |fuchsia_mylib::SomeProtocol::SomeMethod|.
//
// This could be used as part of determining an optimum initial size for a FIDL
// arena or buffer span.
template <typename Method>
constexpr uint32_t ServerReplyBufferSizeInChannel() {
  static_assert(IsFidlTransactionalMessage<internal::TransactionalResponse<Method>>::value,
                "|Method| must be a FIDL method marker type");
  return MaxSizeInChannel<internal::TransactionalResponse<Method>, MessageDirection::kSending>();
}

// Helper to calculate a safe buffer size for use in caller-allocating flavors
// to send a |Method| event, assuming the size of each message is clamped at
// the Zircon channel packet size.
//
// |Method| is a method marker that looks like
// |fuchsia_mylib::SomeProtocol::SomeMethod|.
//
// This could be used as part of determining an optimum initial size for a FIDL
// arena or buffer span.
template <typename Method>
constexpr uint32_t EventReplyBufferSizeInChannel() {
  static_assert(IsFidlTransactionalMessage<internal::TransactionalEvent<Method>>::value,
                "|Method| must be a FIDL method marker type");
  return MaxSizeInChannel<internal::TransactionalEvent<Method>, MessageDirection::kSending>();
}

namespace internal {

// |CallerAllocatingImpl| provides a |Type| which is the corresponding
// caller-allocating messaging implementation given a |ManagedImpl| with managed
// memory allocation. For example,
//
//     CallerAllocatingImpl<WireSyncClientImpl, FidlProtocol>::Type
//
// should be
//
//     WireSyncBufferClientImpl<FidlProtocol>
//
// It is used to easily derive the caller-allocating messaging type given a
// regular messaging type.
template <template <typename FidlProtocol> class ManagedImpl, typename FidlProtocol>
struct CallerAllocatingImpl;

// Associate |WireSyncClientImpl| (managed) and |WireSyncBufferClientImpl|
// (caller-allocating).
template <typename FidlProtocol>
struct CallerAllocatingImpl<WireSyncClientImpl, FidlProtocol> {
  using Type = WireSyncBufferClientImpl<FidlProtocol>;
};

// Associate |WireWeakEventSender| (managed) and |WireWeakBufferEventSender|
// (caller-allocating).
template <typename FidlProtocol>
struct CallerAllocatingImpl<WireWeakEventSender, FidlProtocol> {
  using Type = WireWeakBufferEventSender<FidlProtocol>;
};

// Associate |WireEventSender| (managed) and |WireBufferEventSender|
// (caller-allocating).
template <typename FidlProtocol>
struct CallerAllocatingImpl<WireEventSender, FidlProtocol> {
  using Type = WireBufferEventSender<FidlProtocol>;
};

// A veneer interface object for client/server messaging implementations that
// operate on a borrowed client/server endpoint, and where the implementation
// automatically manages the buffer for message encoding/decoding. Those
// implementations should inherit from this class following CRTP. Example uses
// of this veneer:
//
//   * Making synchronous one-way or two-way calls.
//   * Sending events.
//
// |Derived| implementations must not add any state, only behavior.
template <typename Derived>
struct SyncEndpointManagedVeneer {
 public:
  explicit SyncEndpointManagedVeneer(fidl::internal::AnyUnownedTransport transport)
      : transport_(transport) {}

  // Returns a pointer to the concrete messaging implementation.
  Derived* operator->() && {
    // Required for the static_cast in to work: we are aliasing the base class
    // into |Derived|.
    static_assert(sizeof(Derived) == sizeof(SyncEndpointManagedVeneer),
                  "Derived implementations must not add any state");

    return static_cast<Derived*>(this);
  }

 protected:
  // Used by implementations to access the transport, hence prefixed with an
  // underscore to avoid the unlikely event of a name collision.
  fidl::internal::AnyUnownedTransport _transport() const { return transport_; }

 private:
  fidl::internal::AnyUnownedTransport transport_;
};

// A veneer interface object for client/server messaging implementations that
// operate on a borrowed client/server endpoint, and where the caller provides
// the buffer for message encoding/decoding. Those implementations should
// inherit from this class following CRTP. Example uses of this veneer:
//
//   * Making synchronous one-way or two-way calls.
//   * Sending events.
//
// Compared to |SyncEndpointManagedVeneer|, this class additionally stores an
// allocator, such that subclasses maybe use it during encoding/decoding.
//
// |Derived| implementations must not add any state, only behavior.
template <typename Derived>
struct SyncEndpointBufferVeneer {
  explicit SyncEndpointBufferVeneer(fidl::internal::AnyUnownedTransport transport,
                                    AnyBufferAllocator&& allocator)
      : transport_(transport), allocator_(std::move(allocator)) {}

  // Returns a pointer to the concrete messaging implementation.
  Derived* operator->() {
    // Required for the static_cast in to work: we are aliasing the base class
    // into |Derived|.
    static_assert(sizeof(Derived) == sizeof(SyncEndpointBufferVeneer),
                  "Derived implementations must not add any state");

    return static_cast<Derived*>(this);
  }

 protected:
  // Used by implementations to access the transport, hence prefixed with an
  // underscore to avoid the unlikely event of a name collision.
  fidl::internal::AnyUnownedTransport _transport() const { return transport_; }

  // Used by implementations to access the allocator, hence prefixed with an
  // underscore to avoid the unlikely event of a name collision.
  AnyBufferAllocator& _allocator() { return allocator_; }

 private:
  fidl::internal::AnyUnownedTransport transport_;
  AnyBufferAllocator allocator_;
};

// A veneer interface object for client/server messaging implementations that
// operate on a borrowed client/server endpoint. This class exposes both
// managed and caller-allocating flavors, and delegates to
// |SyncEndpointManagedVeneer| and |SyncEndpointBufferVeneer| respectively.
//
// |SyncImpl| should be the template messaging class,
// e.g. |WireSyncClientImpl| (without passing template parameters).
// |FidlProtocol| should be the protocol marker.
//
// It must not outlive the borrowed endpoint.
template <template <typename FidlProtocol> class SyncImpl, typename FidlProtocol>
class SyncEndpointVeneer final {
 private:
  using CallerAllocatingImpl =
      typename ::fidl::internal::CallerAllocatingImpl<SyncImpl, FidlProtocol>::Type;

 public:
  explicit SyncEndpointVeneer(fidl::internal::AnyUnownedTransport transport)
      : transport_(std::move(transport)) {}

  // Returns a veneer object for the concrete messaging implementation.
  internal::SyncEndpointManagedVeneer<SyncImpl<FidlProtocol>> operator->() && {
    return internal::SyncEndpointManagedVeneer<SyncImpl<FidlProtocol>>(transport_);
  }

  // Returns a veneer object which exposes the caller-allocating API, using
  // the provided |resource| to allocate buffers necessary for each call.
  // The requests and responses (if applicable) will live on those buffers.
  //
  // Examples of supported memory resources are:
  //
  // * |fidl::BufferSpan|, referencing a range of bytes.
  // * |fidl::AnyArena&|, referencing an arena.
  // * Any type for which there is a |MakeAnyBufferAllocator| specialization.
  //   See |AnyBufferAllocator|.
  //
  // The returned object borrows from this object, hence must not outlive
  // the current object.
  //
  // The returned object may be briefly persisted for use over multiple calls:
  //
  //     fidl::Arena my_arena;
  //     auto buffered = fidl::WireCall(client_end).buffer(my_arena);
  //     fidl::WireUnownedResult foo = buffered->FooMethod();
  //     fidl::WireUnownedResult bar = buffered->BarMethod();
  //     ...
  //
  // In this situation, those calls will all use the initially provided memory
  // resource (`my_arena`) to allocate their message buffers. The memory
  // resource won't be reset/overwritten across calls. This means it's possible
  // to access the result from |FooMethod| after making another |BarMethod|
  // call. Note that if a |BufferSpan| is provided as the memory resource,
  // sharing memory resource in this manner may eventually exhaust the capacity
  // of the buffer span since it represents a single fixed size buffer. To reuse
  // (overwrite) the underlying buffer across multiple calls, obtain a new
  // caller-allocating veneer object for each call:
  //
  //     fidl::BufferSpan span(some_large_buffer, size);
  //     auto client = fidl::WireCall(client_end);
  //     client.buffer(span)->FooMethod();
  //     client.buffer(span)->BarMethod();
  //
  template <typename MemoryResource>
  auto buffer(MemoryResource&& resource) {
    return SyncEndpointBufferVeneer<CallerAllocatingImpl>{
        transport_, MakeAnyBufferAllocator(std::forward<MemoryResource>(resource))};
  }

 private:
  fidl::internal::AnyUnownedTransport transport_;
};

template <template <typename FidlProtocol> class SyncImpl, typename FidlProtocol>
class WeakEventSenderVeneer final {
 private:
  using CallerAllocatingImpl =
      typename ::fidl::internal::CallerAllocatingImpl<SyncImpl, FidlProtocol>::Type;

 public:
  explicit WeakEventSenderVeneer(std::weak_ptr<::fidl::internal::AsyncServerBinding> binding)
      : binding_(std::move(binding)) {}

  // Returns a veneer object for sending events with managed memory allocation.
  Arrow<SyncImpl<FidlProtocol>> operator->() { return Arrow<SyncImpl<FidlProtocol>>(binding_); }

  // Returns a veneer object which exposes the caller-allocating API, using
  // the provided |resource| to allocate buffers necessary for each event.
  // See documentation on |SyncEndpointVeneer::buffer| for detailed behavior.
  template <typename MemoryResource>
  auto buffer(MemoryResource&& resource) {
    return Arrow<CallerAllocatingImpl>{
        binding_, MakeAnyBufferAllocator(std::forward<MemoryResource>(resource))};
  }

 private:
  std::weak_ptr<::fidl::internal::AsyncServerBinding> binding_;
};

}  // namespace internal

// A buffer holding data inline, sized specifically for |FidlMethod| and for use
// with synchronous client methods. It can be used to provide request/response
// buffers when using the caller-allocating flavor. For example:
//
//     // All space used for the |Foo| call is allocated from |buffer|.
//     fidl::SyncClientBuffer<MyProtocol::Foo> buffer;
//     fidl::WireUnownedResult result = fidl::WireCall<MyProtocol>(channel)
//         .buffer(buffer.view())
//         ->Foo(args);
//
template <typename FidlMethod>
using SyncClientBuffer =
    internal::InlineMessageBuffer<SyncClientMethodBufferSizeInChannel<FidlMethod>()>;

// A buffer holding data inline, sized specifically for |FidlMethod| and for use
// with asynchronous client methods. It can be used to provide request buffers
// when using the caller-allocating flavor. For example:
//
//     // All space used for the |Foo| call is allocated from |buffer|.
//     fidl::AsyncClientBuffer<MyProtocol::Foo> buffer;
//     fidl::WireClient client(...);
//     client.buffer(buffer.view())->Foo(args);
//
template <typename FidlMethod>
using AsyncClientBuffer =
    internal::InlineMessageBuffer<AsyncClientMethodBufferSizeInChannel<FidlMethod>()>;

// A buffer holding data inline, sized specifically for |FidlMethod| and for
// server-side use. It can be used to provide response buffers when using
// the caller-allocating flavor. For example:
//
//     void Foo(Args args, FooCompleter::Sync& completer) {
//       // All space used for the |Foo| reply is allocated from |buffer|.
//       fidl::ServerBuffer<MyProtocol::Foo> buffer;
//       completer.buffer(buffer.view()).Reply(args);
//     }
//
template <typename FidlMethod>
using ServerBuffer = internal::InlineMessageBuffer<ServerReplyBufferSizeInChannel<FidlMethod>()>;

// A buffer holding data inline, sized specifically for |FidlMethod| and for
// server-side use. It can be used to provide event buffers when using
// the caller-allocating flavor. For example:
//
//     void Foo(Args args, FooCompleter::Sync& completer) {
//       // All space used for the |Foo| reply is allocated from |buffer|.
//       fidl::EventBuffer<MyProtocol::Foo> buffer;
//       fidl::WireSendEvent(binding).buffer(buffer.view())->OnEvent(args);
//     }
//
template <typename FidlMethod>
using EventBuffer = internal::InlineMessageBuffer<EventReplyBufferSizeInChannel<FidlMethod>()>;

}  // namespace fidl

#endif  // LIB_FIDL_LLCPP_SYNC_CALL_H_
