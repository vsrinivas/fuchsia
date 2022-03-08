// Copyright 2022 The Fuchsia Authors. All rights reserved. Use of this source
// code is governed by a BSD-style license that can be found in the LICENSE
// file.

#ifndef LIB_FIDL_LLCPP_INTERNAL_CLIENT_CONTINUATION_H_
#define LIB_FIDL_LLCPP_INTERNAL_CLIENT_CONTINUATION_H_

#include <lib/fidl/llcpp/message.h>
#include <lib/fidl/llcpp/status.h>
#include <lib/fidl/llcpp/wire_messaging_declarations.h>
#include <lib/fit/function.h>
#include <lib/stdcompat/type_traits.h>

#include <memory>
#include <type_traits>

// These definitions implement fxbug.dev/94402, a DSL to teach two-way client
// calls about lifetimes of their result receivers, in doing so discouraging
// use-after-frees. At a high level:
//
// - |CallbackReceiverTraits| is specialized for each individual receiver
//   pointer type (std::shared_ptr, std::weak_ptr, raw pointer, ...).
// - |WeakCallback| either invokes the user callback for handling results, or
//   silently discard it if the receiver object has went away.
// - |WeakCallbackFactory::Then| is a utility function to produce an instance of
//   |WeakCallback|.
//
// When invoking FIDL calls using |Then|, the user passes an appropriate pointer
// to their receiver which is passed to |WeakCallbackFactory::Then| to create
// the desired passivation behavior.
//
// When invoking FIDL calls using |ThenExactlyOnce|, these definitions are not
// used - the supplied continuation is never passivated.
namespace fidl {

// |CallbackReceiverTraits| describes properties of a receiver object pointer in
// a continuation, specifically:
//
// - If the continuation is a pointer-to-member function (&T::Foo), the receiver
//   is defined to be |T|.
// - If the continuation is a lambda like `[] (T* self, Arg arg) {}`, the
//   receiver is defined to be |T|, i.e. the type of the first lambda argument
//   after stripping the pointer.
//
// It should have the following members:
//
// static constexpr bool kIsSmartPointer: whether |Ptr| is a smart pointer.
//
// using Type = ...: the type of the receiver object.
//
// template <typename Continuation> static void TryDeref(...): a function that
// takes a |Ptr| and invokes the |Continuation| with the raw pointer as argument
// only if the pointer has not expired. In case of regular non-smart pointers,
// the pointer is considered to never expire.
//
// Users may add other specializations for their own pointer types (e.g.
// |fbl::RefPtr<T>|).
template <typename Ptr>
struct CallbackReceiverTraits;

template <typename T>
struct CallbackReceiverTraits<T*> {
  static constexpr bool kIsSmartPointer = false;

  using Type = T;

  template <typename Continuation>
  static void TryDeref(T* p, Continuation&& continuation) {
    continuation(p);
  }
};

template <typename T>
struct CallbackReceiverTraits<std::weak_ptr<T>> {
  static constexpr bool kIsSmartPointer = true;

  using Type = T;

  template <typename Continuation>
  static void TryDeref(const std::weak_ptr<T>& p, Continuation&& continuation) {
    if (auto strong = p.lock()) {
      continuation(strong.get());
    }
  }
};

template <typename T>
struct CallbackReceiverTraits<std::shared_ptr<T>> {
  static constexpr bool kIsSmartPointer = true;

  using Type = T;

  template <typename Continuation>
  static void TryDeref(const std::shared_ptr<T>& p, Continuation&& continuation) {
    continuation(p.get());
  }
};

template <typename T>
struct CallbackReceiverTraits<std::unique_ptr<T>> {
  static constexpr bool kIsSmartPointer = true;

  using Type = T;

  template <typename Continuation>
  static void TryDeref(const std::unique_ptr<T>& p, Continuation&& continuation) {
    continuation(p.get());
  }
};

namespace internal {

class ClientControlBlock;

// |WeakCallback| wraps a piece of logic that should be run when the result of a
// two-way FIDL call has arrived, ensuring the wrapped logic is run at most
// once: it either invokes the user continuation for handling results, or
// silently discards it if the receiver object has went away.
//
// |WeakCallback|s should be made from |WeakCallbackFactory::Then|.
template <typename Result, size_t kInlineSize>
struct WeakCallback {
  // The wrapped callback.
  fit::inline_callback<void(Result&), kInlineSize> callback;

  // When true, invoking the continuation will be a no-op when the client object
  // (such as |fidl::WireClient|) has been destroyed.
  //
  // This is used to passivate result callbacks when the receiver object has the
  // same lifetime as the client object, a common occurrence in object-oriented
  // code (e.g. a class which owns a |fidl::WireClient| and also handles
  // asynchronous FIDL call results).
  bool passivate_when_client_object_goes_away;

  // An pointer that expires as soon as the client object is destroyed.
  std::weak_ptr<ClientControlBlock> client_object_lifetime;

  void Run(Result& a) {
    if (passivate_when_client_object_goes_away) {
      if (client_object_lifetime.expired()) {
        return;
      }
    }
    // When the receiver object is managed by a smart pointer, the at-most-once
    // behavior is implemented in the |callback| created in
    // |WeakCallbackFactory::Then|.
    callback(a);
  }
};

// |WeakCallbackFactory| is a utility to create weak callbacks that
// auto-passivate when the receiver object referenced by |ptr| goes away.
//
// Callbacks should take a |Result&| as their last argument.
template <typename Result>
struct WeakCallbackFactory {
  std::weak_ptr<ClientControlBlock> client_object_lifetime;

  // When |Ptr| is a smart pointer as determined by |CallbackReceiverTraits|,
  // the resulting |WeakCallback| passivates only when the smart pointer is a
  // weak pointer that has expired.  When the user passes a raw pointer, the
  // |WeakCallback| passivates only if the client object is destroyed.
  //
  // The callback type (F) should have the following signature in wire methods:
  //
  //     void fn(Ptr receiver [, Args curried_args] , fidl::WireUnownedResult<Method>&);
  //
  // and have the following signature in natural methods:
  //
  //     void fn(Ptr receiver [, Args curried_args] , fitx::result<...>&);
  //
  // In particular, `[, Args curried_args]` are optional arguments that the user
  // could pass to the |Then| function to be forwarded to the callback (function
  // currying).
  template <typename F, typename Ptr, typename... Args>
  auto Then(F&& f, Ptr&& ptr, Args&&... args) && {
    using Traits = ::fidl::CallbackReceiverTraits<cpp20::remove_cvref_t<Ptr>>;
    using Receiver = typename Traits::Type;

    // Best-effort. See https://eel.is/c++draft/expr.prim.lambda#closure-2.1. We
    // cannot rely on implementation-defined behaviors.
    constexpr bool is_captureless_lambda =
        std::is_empty_v<F> && std::is_trivially_destructible_v<F>;
    static_assert(std::is_member_function_pointer_v<F> || is_captureless_lambda,
                  "Only pointer-to-member function or captureless lambdas are allowed.");

    // We pre-apply any optional args using |std::bind| and get rid of them from
    // the argument signature of the returned function.
    //
    // |placeholders::_1| is for the pointer to |Receiver|. |placeholders::_2|
    // is for the |Result&| argument.
    auto partial_applied_fn = std::bind(std::forward<F>(f), std::placeholders::_1,
                                        std::forward<Args>(args)..., std::placeholders::_2);

    auto callback = [fn = std::move(partial_applied_fn),
                     ptr = std::forward<Ptr>(ptr)](Result& result) {
      Traits::TryDeref(ptr, [&](Receiver* raw_ptr) { fn(raw_ptr, result); });
    };
    constexpr static size_t kInlineSize = sizeof(callback);
    return WeakCallback<Result, kInlineSize>{
        .callback = std::move(callback),
        .passivate_when_client_object_goes_away = !Traits::kIsSmartPointer,
        .client_object_lifetime = std::move(client_object_lifetime),
    };
  }
};

}  // namespace internal
}  // namespace fidl

#endif  // LIB_FIDL_LLCPP_INTERNAL_CLIENT_CONTINUATION_H_
