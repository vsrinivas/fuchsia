// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_FIDL_CPP_INCLUDE_LIB_FIDL_CPP_INTERNAL_MAKE_RESPONSE_CONTEXT_H_
#define SRC_LIB_FIDL_CPP_INCLUDE_LIB_FIDL_CPP_INTERNAL_MAKE_RESPONSE_CONTEXT_H_

#include <lib/fidl/cpp/any_error_in.h>
#include <lib/fidl/cpp/unified_messaging.h>
#include <lib/fidl/llcpp/client_base.h>

namespace fidl {
namespace internal {

// |MakeResponseContext| is a helper to create an adaptor from a |ResponseContext|
// to a response/result callback. It returns a raw pointer which deletes itself
// upon the receipt of a response or an error.
template <typename FidlMethod>
ResponseContext* MakeResponseContext(uint64_t ordinal,
                                     ::fidl::ClientCallback<FidlMethod> callback) {
  using CallbackType = ::fidl::ClientCallback<FidlMethod>;

  class ResponseContext final : public ::fidl::internal::ResponseContext {
   public:
    explicit ResponseContext(uint64_t ordinal, CallbackType&& callback)
        : ::fidl::internal::ResponseContext(ordinal), callback_(std::move(callback)) {}

   private:
    ::cpp17::optional<::fidl::UnbindInfo> OnRawResult(
        ::fidl::IncomingMessage&& result,
        ::fidl::internal::IncomingTransportContext transport_context) override {
      using NaturalResponse = ::fidl::Response<FidlMethod>;
      constexpr bool HasApplicationError =
          ::fidl::internal::NaturalMethodTypes<FidlMethod>::HasApplicationError;

      struct DeleteSelf {
        ResponseContext* c;
        ~DeleteSelf() { delete c; }
      } delete_self{this};

      // Check transport error.
      if (!result.ok()) {
        ::fidl::Result<FidlMethod> error = ::fitx::error(result.error());
        callback_(error);
        return cpp17::nullopt;
      }

      ::fitx::result<::fidl::Error, NaturalResponse> decoded =
          NaturalResponse::DecodeTransactional(std::move(result));

      // Check decoding error.
      if (decoded.is_error()) {
        ::fidl::UnbindInfo unbind_info = ::fidl::UnbindInfo(decoded.error_value());
        ::fidl::Result<FidlMethod> error = ::fitx::error(decoded.error_value());
        callback_(error);
        return unbind_info;
      }

      if constexpr (HasApplicationError) {
        // Fold application error.
        if (decoded.value()->result().err().has_value()) {
          ::fidl::Result<FidlMethod> error = ::fitx::error(decoded.value()->result().err().value());
          callback_(error);
        } else {
          ZX_DEBUG_ASSERT(decoded.value()->result().response().has_value());
          if constexpr (::fidl::internal::NaturalMethodTypes<FidlMethod>::IsEmptyStructPayload) {
            // Omit empty structs.
            ::fidl::Result<FidlMethod> value = ::fitx::success();
            callback_(value);
          } else {
            ::fidl::Result<FidlMethod> value =
                ::fitx::ok(std::move(*decoded.value()->result().response().take()));
            callback_(value);
          }
        }
      } else {
        if constexpr (::fidl::internal::NaturalMethodTypes<FidlMethod>::IsAbsentBody) {
          // Absent body.
          ::fidl::Result<FidlMethod> value = ::fitx::success();
          callback_(value);
        } else {
          ::fidl::Result<FidlMethod> value = ::fitx::ok(std::move(*decoded.value()));
          callback_(value);
        }
      }

      return cpp17::nullopt;
    }

    ::fidl::ClientCallback<FidlMethod> callback_;
  };

  return new ResponseContext(ordinal, std::forward<CallbackType>(callback));
}

}  // namespace internal
}  // namespace fidl

#endif  // SRC_LIB_FIDL_CPP_INCLUDE_LIB_FIDL_CPP_INTERNAL_MAKE_RESPONSE_CONTEXT_H_
