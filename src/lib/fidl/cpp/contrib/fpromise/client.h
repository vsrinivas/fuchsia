// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_FIDL_CPP_CONTRIB_FPROMISE_CLIENT_H_
#define SRC_LIB_FIDL_CPP_CONTRIB_FPROMISE_CLIENT_H_

#include <lib/fidl/cpp/internal/thenable.h>
#include <lib/fpromise/bridge.h>

namespace fidl_fpromise {

namespace internal {

// |fpromise::result| uses |void| for when the result does not have a success type.
// On the other hand, |fit::result| simply leaves the |value_type| alias absent.
// This SFINAE converts one scheme to the other.
template <typename T, typename V = void>
struct ValueTypeOrVoid {
  using type = V;
};
template <typename T>
struct ValueTypeOrVoid<T, std::void_t<typename T::value_type>> {
  using type = typename T::value_type;
};

}  // namespace internal

// |as_promise| converts a FIDL asynchronous call in the new C++ bindings
// into a promise. Example usage:
//
//     // Let's say an async FIDL call originally uses a callback.
//     fidl::Client<MyProtocol> client(...);
//     client->Foo({ ... }).Then([] (fidl::Result<MyProtocol::Foo>& result) {
//       assert(result.is_ok());
//     });
//
//     // It can be turned into a promise by wrapping the call with |as_promise|
//     // as opposed to attaching a callback via |Then|:
//     auto p1 = fidl_fpromise::as_promise(client->Foo({ ... }));
//
//     // And used like any other regular promise:
//     auto p2 = p1.then([] (auto& result) {
//       assert(result.is_ok());
//     });
//     some_executor.schedule_task(std::move(p2));
//
// The signature of the resulting promise is akin to
//
//     fpromise::promise<SuccessType, ErrorType>
//
// where |SuccessType| is the payload type for when the FIDL call succeeds, or
// |void| if the FIDL call has an empty/zero-argument payload.
// where |ErrorType| is |fidl::Error| if the FIDL call does not use application
// errors, and |fidl::ErrorsIn<MyProtocol::FidlMethod>| otherwise. |MyProtocol|
// and |FidlMethod| are all placeholders to be replaced by the actual protocol
// and method names.
template <typename FidlMethod>
auto as_promise(::fidl::internal::NaturalThenable<FidlMethod>&& thenable) {
  using ResultType = typename FidlMethod::Protocol::Transport::template Result<FidlMethod>;
  using E = typename ResultType::error_type;
  using V = typename internal::ValueTypeOrVoid<ResultType>::type;
  ::fpromise::bridge<V, E> bridge;
  std::move(thenable).ThenExactlyOnce(
      [completer = std::move(bridge.completer)](auto&& result) mutable {
        if (result.is_ok()) {
          if constexpr (std::is_same_v<V, void>) {
            completer.complete_ok();
          } else {
            completer.complete_ok(std::move(result.value()));
          }
        } else {
          completer.complete_error(std::move(result.error_value()));
        }
      });
  return bridge.consumer.promise();
}

}  // namespace fidl_fpromise

#endif  // SRC_LIB_FIDL_CPP_CONTRIB_FPROMISE_CLIENT_H_
