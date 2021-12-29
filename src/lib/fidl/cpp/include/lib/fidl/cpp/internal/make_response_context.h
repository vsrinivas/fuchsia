// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_FIDL_CPP_INCLUDE_LIB_FIDL_CPP_INTERNAL_MAKE_RESPONSE_CONTEXT_H_
#define SRC_LIB_FIDL_CPP_INCLUDE_LIB_FIDL_CPP_INTERNAL_MAKE_RESPONSE_CONTEXT_H_

#include <lib/fidl/cpp/unified_messaging.h>
#include <lib/fidl/llcpp/client_base.h>

namespace fidl {
namespace internal {

// |MakeResponseContext| is a helper to create an adaptor from a |ResponseContext|
// to a response/result callback. It returns a raw pointer which deletes itself
// upon the receipt of a response or an error.
template <typename NaturalResponse, typename CallbackType>
ResponseContext* MakeResponseContext(uint64_t ordinal, CallbackType&& callback) {
  class ResponseContext final : public ::fidl::internal::ResponseContext {
   public:
    explicit ResponseContext(uint64_t ordinal, CallbackType&& callback)
        : ::fidl::internal::ResponseContext(ordinal), callback_(std::move(callback)) {}

   private:
    ::cpp17::optional<::fidl::UnbindInfo> OnRawResult(
        ::fidl::IncomingMessage&& result,
        ::fidl::internal::IncomingTransportContext transport_context) override {
      using IsResultCallback =
          std::is_invocable<CallbackType, ::fitx::result<::fidl::Error, NaturalResponse>&>;
      struct DeleteSelf {
        ResponseContext* c;
        ~DeleteSelf() { delete c; }
      } delete_self{this};

      // Check transport error.
      if (!result.ok()) {
        ::fitx::result<::fidl::Error, NaturalResponse> error = ::fitx::error(result.error());
        if constexpr (IsResultCallback::value) {
          callback_(error);
        }
        return cpp17::nullopt;
      }

      ::fitx::result<::fidl::Error, NaturalResponse> decoded =
          NaturalResponse::DecodeTransactional(std::move(result));

      // Check decoding error.
      if (decoded.is_error()) {
        ::fidl::UnbindInfo unbind_info = ::fidl::UnbindInfo(decoded.error_value());
        if constexpr (IsResultCallback::value) {
          callback_(decoded);
        }
        return unbind_info;
      }

      // Success callback.
      if constexpr (IsResultCallback::value) {
        callback_(decoded);
      } else {
        callback_(decoded.value());
      }
      return cpp17::nullopt;
    }

    CallbackType callback_;
  };

  return new ResponseContext(ordinal, std::forward<CallbackType>(callback));
}

}  // namespace internal
}  // namespace fidl

#endif  // SRC_LIB_FIDL_CPP_INCLUDE_LIB_FIDL_CPP_INTERNAL_MAKE_RESPONSE_CONTEXT_H_
