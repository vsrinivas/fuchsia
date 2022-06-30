// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_CPP_WIRE_INCLUDE_LIB_FIDL_CPP_WIRE_INTERNAL_MAKE_RESPONSE_CONTEXT_H_
#define LIB_FIDL_CPP_WIRE_INCLUDE_LIB_FIDL_CPP_WIRE_INTERNAL_MAKE_RESPONSE_CONTEXT_H_

#include <lib/fidl/cpp/wire/wire_messaging.h>

#include <memory>
#include <type_traits>

namespace fidl::internal {

template <typename FidlMethod, typename Continuation>
fidl::WireResponseContext<FidlMethod>* MakeWireResponseContext(Continuation&& callback) {
  using ResultType = fidl::internal::WireUnownedResultType<FidlMethod>;

  class ResponseContext final : public fidl::WireResponseContext<FidlMethod> {
   public:
    explicit ResponseContext(Continuation cb) : cb_(std::move(cb)) {}

    void OnResult(ResultType& result) override {
      cb_(result);
      delete this;
    }

   private:
    Continuation cb_;
  };

  return new ResponseContext(std::forward<Continuation>(callback));
}

}  // namespace fidl::internal

#endif  // LIB_FIDL_CPP_WIRE_INCLUDE_LIB_FIDL_CPP_WIRE_INTERNAL_MAKE_RESPONSE_CONTEXT_H_
