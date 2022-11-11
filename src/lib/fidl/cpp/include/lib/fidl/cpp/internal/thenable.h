// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_FIDL_CPP_INCLUDE_LIB_FIDL_CPP_INTERNAL_THENABLE_H_
#define SRC_LIB_FIDL_CPP_INCLUDE_LIB_FIDL_CPP_INTERNAL_THENABLE_H_

#include <lib/fidl/cpp/internal/make_response_context.h>
#include <lib/fidl/cpp/internal/natural_message_encoder.h>
#include <lib/fidl/cpp/wire/client_base.h>
#include <lib/fidl/cpp/wire/internal/client_continuation.h>
#include <lib/fidl/cpp/wire/internal/thenable.h>
#include <lib/fidl/cpp/wire/message.h>

namespace fidl::internal {

// |NaturalThenable| kick-starts a two-way client FIDL call: it stores an
// encoded message ready to be sent, and sends it once the user attaches a
// continuation for handling the result.
//
// It exposes an interface similar to a future: the user must call |Then| or
// |ThenExactlyOnce| to specify a continuation, after which this object is
// consumed.
//
// Refer to the comments below for their impact on object lifetimes.
template <typename FidlMethod>
class [[nodiscard]] NaturalThenable : private ThenableBase {
 private:
  // |MessageSendOp| executes custom logic to send a message after |Then{ExactlyOnce}|.
  // TODO(fxbug.dev/94402): Encapsulate them inside |NaturalMessageEncoder|.
  using MessageSendOp =
      fit::inline_callback<void(ThenableBase*, NaturalMessageEncoder&, ResponseContext*),
                           sizeof(void*) * 6>;  // We need to be able to fit |fdf::Arena|.

 public:
  template <typename EncodeCallback>
  explicit NaturalThenable(ClientBase* client_base, fidl::WriteOptions options,
                           const TransportVTable* vtable, uint64_t ordinal,
                           MessageDynamicFlags dynamic_flags, EncodeCallback encode_callback,
                           MessageSendOp message_send_op)
      : ThenableBase(client_base, std::move(options)),
        encoded_(vtable, ordinal, dynamic_flags),
        message_send_op_(std::move(message_send_op)) {
    encode_callback(encoded_);
  }

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
  //         client_->SomeMethod(request).Then(fit::bind_member<&Foo::HandleResult>(this));
  //       }
  //
  //       void HandleResult(fidl::Result<SomeMethod>& result) {
  //         // Handle the result from making the call in |DoSomething|.
  //         // If |Foo| is destroyed, any pending callbacks are discarded.
  //       }
  //
  //      private:
  //       fidl::Client<MyProtocol> client_;
  //     };
  //
  // When using |SharedClient|, note that |Then| alone is not sufficient for
  // memory safety: |SharedClient| allows the user to destroy the client
  // from an arbitrary thread, which may race with in-progress callbacks. Always
  // use thread-safe reference counting or teardown observers to maintain
  // correct receiver lifetime.
  template <typename Fn>
  void Then(Fn&& fn) && {
    using ResultType = typename FidlMethod::Protocol::Transport::template Result<FidlMethod>;
    std::move(*this).ThenExactlyOnce(MakeResponseContext<FidlMethod>(
        internal::WireOrdinal<FidlMethod>::value,
        WeakCallbackFactory<ResultType>{client_base()->client_object_lifetime()}.Then(
            std::forward<Fn>(fn))));
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
  void ThenExactlyOnce(::fidl::ClientCallback<FidlMethod> callback) && {
    std::move(*this).ThenExactlyOnce(MakeResponseContext<FidlMethod>(
        internal::WireOrdinal<FidlMethod>::value, std::move(callback)));
  }

  NaturalThenable(NaturalThenable&& other) noexcept = delete;
  NaturalThenable& operator=(NaturalThenable&& other) noexcept = delete;
  NaturalThenable(const NaturalThenable& other) noexcept = delete;
  NaturalThenable& operator=(const NaturalThenable& other) noexcept = delete;

 private:
  void ThenExactlyOnce(::fidl::internal::ResponseContext* context) && {
    message_send_op_(this, encoded_, context);
  }

  NaturalMessageEncoder encoded_;
  MessageSendOp message_send_op_;
};

}  // namespace fidl::internal

#endif  // SRC_LIB_FIDL_CPP_INCLUDE_LIB_FIDL_CPP_INTERNAL_THENABLE_H_
