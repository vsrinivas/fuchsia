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
  using ResultType = typename FidlMethod::Protocol::Transport::template Result<FidlMethod>;
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
      constexpr bool IsAbsentBody = ::fidl::internal::NaturalMethodTypes<FidlMethod>::IsAbsentBody;

      struct DeleteSelf {
        ResponseContext* c;
        ~DeleteSelf() { delete c; }
      } delete_self{this};

      // Check transport error.
      if (!result.ok()) {
        ResultType error = ::fitx::error(result.error());
        callback_(error);
        return cpp17::nullopt;
      }

      ::fitx::result decoded = [&] {
        if constexpr (IsAbsentBody) {
          return DecodeTransactionalMessage(std::move(result));
        } else {
          using Body = typename MessageTraits<NaturalResponse>::Payload;
          return DecodeTransactionalMessage<Body>(std::move(result));
        }
      }();

      // Check decoding error.
      if (decoded.is_error()) {
        ::fidl::UnbindInfo unbind_info = ::fidl::UnbindInfo(decoded.error_value());
        ResultType error = ::fitx::error(decoded.error_value());
        callback_(error);
        return unbind_info;
      }

      if constexpr (IsAbsentBody) {
        // Absent body.
        ResultType value = ::fitx::success();
        callback_(value);
      } else {
        NaturalResponse response =
            NaturalMessageConverter<NaturalResponse>::FromDomainObject(std::move(decoded.value()));
        if constexpr (HasApplicationError) {
          // Fold application error.
          if (response.is_error()) {
            ResultType error = response.take_error();
            callback_(error);
          } else {
            ZX_DEBUG_ASSERT(response.is_ok());
            if constexpr (::fidl::internal::NaturalMethodTypes<FidlMethod>::IsEmptyStructPayload) {
              // Omit empty structs.
              ResultType value = ::fitx::success();
              callback_(value);
            } else {
              ResultType value = response.take_value();
              callback_(value);
            }
          }
        } else {
          ResultType value = ::fitx::ok(std::move(response));
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
