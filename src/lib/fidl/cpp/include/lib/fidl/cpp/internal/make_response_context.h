// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_FIDL_CPP_INCLUDE_LIB_FIDL_CPP_INTERNAL_MAKE_RESPONSE_CONTEXT_H_
#define SRC_LIB_FIDL_CPP_INCLUDE_LIB_FIDL_CPP_INTERNAL_MAKE_RESPONSE_CONTEXT_H_

#include <lib/fidl/cpp/any_error_in.h>
#include <lib/fidl/cpp/unified_messaging.h>
#include <lib/fidl/cpp/wire/client_base.h>

#include <optional>

namespace fidl {
namespace internal {

// |DecodeResponseAndFoldError| decodes an incoming message |incoming| returns
// a transport-specific result type (e.g. |fidl::Result| for Zircon channel
// transport). In doing so it combines any FIDL application error from the error
// syntax with transport errors.
//
// If a terminal error occurred which warrants unbinding, |out_maybe_unbind|
// will be populated with a reason if not nullptr.
template <typename FidlMethod>
auto DecodeResponseAndFoldError(::fidl::IncomingHeaderAndMessage&& incoming,
                                ::std::optional<::fidl::UnbindInfo>* out_maybe_unbind) {
  using ResultType = typename FidlMethod::Protocol::Transport::template Result<FidlMethod>;
  using NaturalResponse = ::fidl::Response<FidlMethod>;
  constexpr bool IsAbsentBody = ::fidl::internal::NaturalMethodTypes<FidlMethod>::IsAbsentBody;
  constexpr bool HasApplicationError =
      ::fidl::internal::NaturalMethodTypes<FidlMethod>::HasApplicationError;
  constexpr bool HasTransportError =
      ::fidl::internal::NaturalMethodTypes<FidlMethod>::HasTransportError;

  // Check error from the underlying transport.
  if (!incoming.ok()) {
    ResultType error = ::fit::error(incoming.error());
    if (out_maybe_unbind != nullptr) {
      out_maybe_unbind->emplace(incoming.error());
    }
    return error;
  }

  ::fit::result decoded = [&] {
    if constexpr (IsAbsentBody) {
      return DecodeTransactionalMessage(std::move(incoming));
    } else {
      using Body = typename MessageTraits<NaturalResponse>::Payload;
      return DecodeTransactionalMessage<Body>(std::move(incoming));
    }
  }();

  // Check decoding error.
  if (decoded.is_error()) {
    ResultType error = ::fit::error(decoded.error_value());
    if (out_maybe_unbind != nullptr) {
      out_maybe_unbind->emplace(decoded.error_value());
    }
    return error;
  }

  if constexpr (IsAbsentBody) {
    // Absent body.
    ResultType value = ::fit::success();
    return value;
  } else {
    auto& domain_object = decoded.value();
    if constexpr (HasApplicationError) {
      if (domain_object.result().err().has_value()) {
        ResultType value = ::fit::error(std::move(domain_object.result().err().value()));
        return value;
      }
    }
    if constexpr (HasTransportError) {
      if (domain_object.result().transport_err().has_value()) {
        ::fidl::internal::TransportErr transport_err =
            domain_object.result().transport_err().value();
        switch (transport_err) {
          case ::fidl::internal::TransportErr::kUnknownMethod: {
            ResultType value = ::fit::error(::fidl::Error::UnknownMethod());
            return value;
          }
        }
      }
    }
    if constexpr (HasApplicationError || HasTransportError) {
      constexpr bool IsEmptyStructPayload =
          ::fidl::internal::NaturalMethodTypes<FidlMethod>::IsEmptyStructPayload;

      ZX_DEBUG_ASSERT(domain_object.result().response().has_value());
      if constexpr (IsEmptyStructPayload) {
        ResultType value = ::fit::success();
        return value;
      } else {
        ResultType value = ::fit::ok(std::move(domain_object.result().response().value()));
        return value;
      }
    } else {
      ResultType value = ::fit::ok(std::move(domain_object));
      return value;
    }
  }
}

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
    std::optional<::fidl::UnbindInfo> OnRawResult(
        ::fidl::IncomingHeaderAndMessage&& result,
        ::fidl::internal::MessageStorageViewBase* storage_view) override {
      std::optional<fidl::UnbindInfo> maybe_unbind;
      ResultType value = DecodeResponseAndFoldError<FidlMethod>(std::move(result), &maybe_unbind);
      callback_(value);
      delete this;
      return maybe_unbind;
    }

    ::fidl::ClientCallback<FidlMethod> callback_;
  };

  return new ResponseContext(ordinal, std::forward<CallbackType>(callback));
}

}  // namespace internal
}  // namespace fidl

#endif  // SRC_LIB_FIDL_CPP_INCLUDE_LIB_FIDL_CPP_INTERNAL_MAKE_RESPONSE_CONTEXT_H_
