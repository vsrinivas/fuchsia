// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_LLCPP_INCLUDE_LIB_FIDL_LLCPP_INTERNAL_THENABLE_H_
#define LIB_FIDL_LLCPP_INCLUDE_LIB_FIDL_LLCPP_INTERNAL_THENABLE_H_

#include <lib/async/dispatcher.h>
#include <lib/fidl/llcpp/client_base.h>
#include <lib/fidl/llcpp/internal/client_continuation.h>
#include <lib/fidl/llcpp/internal/make_response_context.h>
#include <lib/fidl/llcpp/message.h>

#include <type_traits>

namespace fidl::internal {

// Base class of various thenable types, regardless of domain object flavor.
//
// It enforces an invariant that |SendTwoWay| must be called exactly once.
class ThenableBase {
 public:
  explicit ThenableBase(ClientBase* client_base, fidl::WriteOptions options);
  ~ThenableBase();

  ClientBase* client_base() const { return client_base_; }

  void SendTwoWay(fidl::OutgoingMessage& message, ResponseContext* context);

 private:
  ClientBase* client_base_;
  fidl::WriteOptions options_;
};

// |WireThenableImpl| kick-starts a two-way client FIDL call: it stores an
// encoded wire message ready to be sent, and sends it once the user attaches a
// continuation for handling the result.
//
// It exposes an interface similar to a future: the user must call |Then| or
// |ThenExactlyOnce| to specify a continuation, after which this object is
// consumed.
//
// There are different kinds of continuation supported:
//
//   - a callable object
//   - |fidl::WireResponseContext|
//
// Refer to the comments below for their impact on object lifetimes.
template <typename FidlMethod, typename EncodedRequestMessage>
class [[nodiscard]] WireThenableImpl : private ThenableBase {
 public:
  template <typename... Args>
  explicit WireThenableImpl(ClientBase* client_base, fidl::WriteOptions options, Args&&... args)
      : ThenableBase(client_base, std::move(options)),
        request_message_(std::forward<Args>(args)...) {}

  // |Then| takes a callback, and implements "at most once" semantics: it
  // invokes the callback at most once until the client goes away. In other
  // words, the callback passivates when the client object goes away.
  //
  // This is useful when the callback receiver object has the same lifetime as
  // the client object. It is an optimization for when the client and the
  // receiver (typically `this`) are tightly coupled and always destroyed
  // together in a sequential context, allowing us to avoid additional
  // cancellation mechanisms such as `WeakPtrFactory`. When the client is a
  // member field of `this`, the answer is almost always using `This` to silence
  // pending callbacks at destruction time.
  //
  // Example syntax:
  //
  //     class Foo {
  //      public:
  //       void DoSomething() {
  //         client_.SomeMethod(request).Then(fit::bind_member<&Foo::HandleResult>(this));
  //       }
  //
  //       void HandleResult(fidl::WireUnownedResult<SomeMethod>& result) {
  //         // Handle the result from making the call in |DoSomething|.
  //         // If |Foo| is destroyed, any pending callbacks are discarded.
  //       }
  //
  //      private:
  //       fidl::WireClient<MyProtocol> client_;
  //     };
  //
  // When using |WireSharedClient|, note that |Then| alone is not sufficient for
  // memory safety: |WireSharedClient| allows the user to destroy the client
  // from an arbitrary thread, which may race with in-progress callbacks. Always
  // use thread-safe reference counting or teardown observers to maintain
  // correct receiver lifetime.
  template <typename Fn>
  void Then(Fn&& fn) && {
    std::move(*this).ThenExactlyOnce(MakeWireResponseContext<FidlMethod>(
        WeakCallbackFactory<fidl::internal::WireUnownedResultType<FidlMethod>>{
            client_base()->client_object_lifetime()}
            .Then(std::forward<Fn>(fn))));
  }

  // This |ThenExactlyOnce| overload takes an arbitrary callable. |callback| is
  // called exactly once, even after the client object was destroyed. It is the
  // responsibility of the user to write any appropriate cancellation logic;
  // they have to be careful about the lifetimes of any objects captured by the
  // callable.
  //
  // NOTE: This should almost never be used if the lambda captures `this` and
  // the client is a member of `this`, because the client may asynchronously
  // notify the outer object of errors after its destruction, to prevent
  // re-entrancy. Prefer |Then| over |ThenExactlyOnce| when writing
  // object-oriented code.
  //
  // This method is useful in unit tests, and for integrating with objects that
  // want "exactly once" semantics, and which could be retained forever without
  // breaking memory safety:
  //
  //   - fpromise::promise completers
  //   - FIDL server method completers, if the server is not unbound at the same
  //     event loop iteration when the client is destroyed.
  //
  void ThenExactlyOnce(::fidl::WireClientCallback<FidlMethod> callback) && {
    std::move(*this).ThenExactlyOnce(MakeWireResponseContext<FidlMethod>(std::move(callback)));
  }

  // This |ThenExactlyOnce| overload is suitable when one needs complete control
  // over memory allocation. Instead of implicitly heap allocating the necessary
  // bookkeeping for in-flight operations, this method takes a raw pointer to a
  // |fidl::WireResponseContext<FidlMethod>|, which may be allocated via any
  // means as long as it outlives the duration of this async FIDL call. Refer to
  // documentation on |fidl::WireResponseContext|.
  //
  // Similarly, the user is responsible for ensuring |context| stays alive until
  // the result has arrived, potentially after destroying the client object.
  void ThenExactlyOnce(::fidl::WireResponseContext<FidlMethod>* context) && {
    SendTwoWay(request_message_.GetOutgoingMessage(), context);
  }

  WireThenableImpl(WireThenableImpl&& other) noexcept = delete;
  WireThenableImpl& operator=(WireThenableImpl&& other) noexcept = delete;
  WireThenableImpl(const WireThenableImpl& other) noexcept = delete;
  WireThenableImpl& operator=(const WireThenableImpl& other) noexcept = delete;

 private:
  EncodedRequestMessage request_message_;
};

}  // namespace fidl::internal

#endif  // LIB_FIDL_LLCPP_INCLUDE_LIB_FIDL_LLCPP_INTERNAL_THENABLE_H_
