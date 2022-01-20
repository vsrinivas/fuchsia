// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_LLCPP_INTERNAL_CLIENT_DETAILS_H_
#define LIB_FIDL_LLCPP_INTERNAL_CLIENT_DETAILS_H_

#include <lib/fidl/llcpp/internal/arrow.h>
#include <lib/fidl/llcpp/message.h>
#include <lib/fidl/llcpp/result.h>
#include <lib/fidl/llcpp/wire_messaging_declarations.h>
#include <lib/fit/function.h>
#include <lib/stdcompat/variant.h>

#include <memory>
#include <type_traits>

namespace fidl {
namespace internal {

// The base class for all asynchronous event handlers, regardless of domain
// object flavor or protocol type.
class AsyncEventHandler {
 public:
  virtual ~AsyncEventHandler() = default;

  // |on_fidl_error| is invoked when the client encounters a terminal error:
  //
  // - The server-end of the channel was closed.
  // - An epitaph was received.
  // - Decoding or encoding failed.
  // - An invalid or unknown message was encountered.
  // - Error waiting on, reading from, or writing to the channel.
  //
  // It uses snake-case to differentiate from virtual methods corresponding to
  // FIDL events.
  //
  // |info| contains the detailed reason for stopping message dispatch.
  //
  // |on_fidl_error| will be invoked on a dispatcher thread, unless the user
  // shuts down the async dispatcher while there are active client bindings
  // associated with it. In that case, |on_fidl_error| will be synchronously
  // invoked on the thread calling dispatcher shutdown.
  virtual void on_fidl_error(::fidl::UnbindInfo error) {}
};

// |IncomingEventDispatcher| decodes events and invokes the corresponding
// methods in an event handler. It is the client side counterpart to the server
// side |IncomingMessageDispatcher|.
//
// On the server side, the server implementation would inherit from
// |IncomingMessageDispatcher|, which decodes and invokes methods on the
// subclass. Over on the client side, the event dispatcher and event handlers
// are unrelated by inheritance, because the user may pass a |nullptr| event
// handler to ignore all events.
class IncomingEventDispatcherBase {
 public:
  explicit IncomingEventDispatcherBase(AsyncEventHandler* event_handler)
      : event_handler_(event_handler) {}

  virtual ~IncomingEventDispatcherBase() = default;

  // Dispatches an incoming event.
  //
  // This should be implemented by the generated messaging layer.
  //
  // ## Handling events
  //
  // If |event_handler| is null, the implementation should perform all the
  // checks that the message is valid and a recognized event, but not actually
  // invoke the event handler.
  //
  // If |event_handler| is present, it should point to a event handler subclass
  // which corresponds to the protocol of |ClientImpl|. This constraint is
  // typically enforced when creating the client.
  //
  // ## Message ownership
  //
  // If a matching event handler is found, |msg| is then consumed, regardless of
  // decoding error. Otherwise, |msg| is not consumed.
  //
  // ## Return value
  //
  // If errors occur during dispatching, the function will return an
  // |UnbindInfo| describing the error. Otherwise, it will return
  // |std::nullopt|.
  virtual std::optional<UnbindInfo> DispatchEvent(
      fidl::IncomingMessage& msg, internal::IncomingTransportContext transport_context) = 0;

  AsyncEventHandler* event_handler() const { return event_handler_; }

 private:
  AsyncEventHandler* event_handler_;
};

using AnyIncomingEventDispatcher = Any<IncomingEventDispatcherBase>;

// |IncomingEventDispatcher| is the corresponding event dispatcher for a
// protocol whose event handler is of type |EventHandler|. The generated code
// should contain a |fidl::internal::WireEventDispatcher<Protocol>| which
// subclasses |IncomingEventDispatcher<EventHandler>|, and dispatches events for
// that protocol.
template <typename EventHandler>
class IncomingEventDispatcher : public IncomingEventDispatcherBase {
 public:
  explicit IncomingEventDispatcher(EventHandler* event_handler)
      : IncomingEventDispatcherBase(event_handler) {}

  EventHandler* event_handler() const {
    // This static_cast is safe because the only way to get an |event_handler|
    // is from the constructor, which takes |ProtocolEventHandler*|.
    return static_cast<EventHandler*>(IncomingEventDispatcherBase::event_handler());
  }
};

template <typename Protocol>
AnyIncomingEventDispatcher MakeAnyEventDispatcher(
    fidl::WireAsyncEventHandler<Protocol>* event_handler) {
  AnyIncomingEventDispatcher event_dispatcher;
  event_dispatcher.emplace<fidl::internal::WireEventDispatcher<Protocol>>(event_handler);
  return event_dispatcher;
}

class ClientBase;

// |ClientImplBase| stores the core state for client messaging
// implementations that use |ClientBase|, and where the message encoding buffers
// are managed internally by the implementation.
class ClientImplBase {
 public:
  explicit ClientImplBase(ClientBase* client_base) : client_base_(client_base) {}

 protected:
  // Used by implementations to access the transport, hence prefixed with an
  // underscore to avoid the unlikely event of a name collision.
  ClientBase* _client_base() const { return client_base_; }

 private:
  ClientBase* client_base_;
};

// |BufferClientImplBase| stores the core state for client messaging
// implementations that use |ClientBase|, and where the message encoding buffers
// are provided by an allocator.
class BufferClientImplBase {
 public:
  explicit BufferClientImplBase(ClientBase* client_base, AnyBufferAllocator&& allocator)
      : client_base_(client_base), allocator_(std::move(allocator)) {}

 protected:
  // Used by implementations to access the transport, hence prefixed with an
  // underscore to avoid the unlikely event of a name collision.
  ClientBase* _client_base() const { return client_base_; }

  // Used by implementations to access the allocator, hence prefixed with an
  // underscore to avoid the unlikely event of a name collision.
  AnyBufferAllocator& _allocator() { return allocator_; }

 private:
  ClientBase* client_base_;
  AnyBufferAllocator allocator_;
};

}  // namespace internal

// A type-erasing object to inform the user the completion of bindings teardown.
//
// Teardown observers are constructed by public helper functions such as
// |fidl::ObserveTeardown|. Adding this layer of indirection allows extending
// teardown observation to custom user types (for example, by defining another
// helper function) without changing this class.
class AnyTeardownObserver final {
 public:
  // Creates an observer that notifies teardown completion by destroying
  // |object|.
  template <typename T>
  static AnyTeardownObserver ByOwning(T object) {
    return AnyTeardownObserver([object = std::move(object)] {});
  }

  // Creates an observer that notifies teardown completion by invoking
  // |callback|, then destroying |callback|.
  template <typename Callable>
  static AnyTeardownObserver ByCallback(Callable&& callback) {
    return AnyTeardownObserver(fit::closure(std::forward<Callable>(callback)));
  }

  // Creates an observer that does nothing on teardown completion.
  static AnyTeardownObserver Noop() {
    return AnyTeardownObserver([] {});
  }

  // Notify teardown completion. This consumes the observer.
  void Notify() && { callback_(); }

  AnyTeardownObserver(const AnyTeardownObserver& other) noexcept = delete;
  AnyTeardownObserver& operator=(const AnyTeardownObserver& other) noexcept = delete;
  AnyTeardownObserver(AnyTeardownObserver&& other) noexcept = default;
  AnyTeardownObserver& operator=(AnyTeardownObserver&& other) noexcept = default;

  ~AnyTeardownObserver() {
    // |callback_| must be expended by the bindings runtime.
    ZX_DEBUG_ASSERT(callback_ == nullptr);
  }

 private:
  using Closure = fit::callback<void()>;

  explicit AnyTeardownObserver(Closure&& callback) : callback_(std::move(callback)) {
    ZX_DEBUG_ASSERT(callback_ != nullptr);
  }

  Closure callback_;
};

}  // namespace fidl

#endif  // LIB_FIDL_LLCPP_INTERNAL_CLIENT_DETAILS_H_
