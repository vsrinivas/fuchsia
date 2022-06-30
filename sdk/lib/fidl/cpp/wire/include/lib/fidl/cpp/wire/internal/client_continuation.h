// Copyright 2022 The Fuchsia Authors. All rights reserved. Use of this source
// code is governed by a BSD-style license that can be found in the LICENSE
// file.

#ifndef LIB_FIDL_CPP_WIRE_INCLUDE_LIB_FIDL_CPP_WIRE_INTERNAL_CLIENT_CONTINUATION_H_
#define LIB_FIDL_CPP_WIRE_INCLUDE_LIB_FIDL_CPP_WIRE_INTERNAL_CLIENT_CONTINUATION_H_

#include <lib/fidl/cpp/wire/message.h>
#include <lib/fidl/cpp/wire/status.h>
#include <lib/fidl/cpp/wire/wire_messaging_declarations.h>
#include <lib/fit/function.h>

#include <memory>
#include <type_traits>

// These definitions implement fxbug.dev/94402, a DSL to teach two-way client
// calls about lifetimes of their result receivers, in doing so discouraging
// use-after-frees. At a high level:
//
// - |WeakCallback| either invokes the user callback for handling results, or
//   silently discard it if the receiver object has went away.
// - |WeakCallbackFactory::Then| is a utility function to produce an instance of
//   |WeakCallback|.
//
// When invoking FIDL calls using |Then|, the user passes a callback which is
// passed to |WeakCallbackFactory::Then| to create the desired passivation
// behavior.
//
// When invoking FIDL calls using |ThenExactlyOnce|, these definitions are not
// used - the supplied continuation is never passivated.
namespace fidl::internal {

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

  // An pointer that expires as soon as the client object is destroyed.
  std::weak_ptr<ClientControlBlock> client_object_lifetime;

  void Run(Result& a) {
    if (client_object_lifetime.expired()) {
      return;
    }
    callback(a);
  }

  // Implement a callable interface.
  void operator()(Result& a) { Run(a); }
};

// |WeakCallbackFactory| is a utility to create weak callbacks that
// auto-passivate when the client goes away.
template <typename Result>
struct WeakCallbackFactory {
  std::weak_ptr<ClientControlBlock> client_object_lifetime;

  template <typename Fn>
  auto Then(Fn&& fn) && {
    constexpr static size_t kInlineSize = sizeof(Fn);
    return WeakCallback<Result, kInlineSize>{
        .callback = std::forward<Fn>(fn),
        .client_object_lifetime = std::move(client_object_lifetime),
    };
  }
};

}  // namespace fidl::internal

#endif  // LIB_FIDL_CPP_WIRE_INCLUDE_LIB_FIDL_CPP_WIRE_INTERNAL_CLIENT_CONTINUATION_H_
