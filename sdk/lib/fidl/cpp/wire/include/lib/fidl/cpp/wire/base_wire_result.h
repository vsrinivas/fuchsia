// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_CPP_WIRE_INCLUDE_LIB_FIDL_CPP_WIRE_BASE_WIRE_RESULT_H_
#define LIB_FIDL_CPP_WIRE_INCLUDE_LIB_FIDL_CPP_WIRE_BASE_WIRE_RESULT_H_

#include <lib/fidl/cpp/wire/internal/transport_err.h>
#include <lib/fidl/cpp/wire/message.h>
#include <lib/fidl/cpp/wire/status.h>
#include <lib/fidl/cpp/wire/wire_messaging_declarations.h>

#include <type_traits>

namespace fidl {

// Returns true if the FidlMethod uses the result union (which is the case if it
// is flexible or uses error syntax).
template <typename FidlMethod>
constexpr bool MethodHasResultUnion() {
  return FidlMethod::kHasApplicationError || FidlMethod::kHasTransportError;
}

// Returns true if the method requires an |Unwrap| and related accessors for the
// return value. If false, the |BaseWireResult| will not provide the |Unwrap|
// and related methods.
template <typename FidlMethod>
constexpr bool MethodHasUnwrapAccessors() {
  return FidlMethod::kHasNonEmptyPayload || FidlMethod::kHasApplicationError;
}

namespace internal {
// |BaseWireResultStorage| is a set of templates which define how the |Unwrap| method in
// |BaseWireResult| will access the decoded value from a two-way method call.
//
// This is only needed for two-way methods which have a non-empty payload or an
// application error. For one-way methods and two-way methods with an empty
// payload and no use of error syntax, |BaseWireResult| doesn't store anything.
template <typename FidlMethod, typename Enable = void>
struct BaseWireResultStorage;

// Defines the storage type for a method which does not use error syntax. This
// is just the same pointer returned by |Unwrap|. For a strict method, this is
// just a pointer to the body of the |TransactionalResponse|. For a flexible
// method, this is a pointer to the success payload of the result union, if the
// method succeeded.
template <typename FidlMethod>
struct BaseWireResultStorage<FidlMethod,
                             std::enable_if_t<!FidlMethod::kHasApplicationError, void>> {
  BaseWireResultStorage() = delete;

  using Type = WireResultUnwrapType<FidlMethod>*;
};

// Defines the storage for a method which uses error syntax. For these methods
// if the result union is the success or application error variants, the result
// is stored in a |fit::result| which has the application error type as its
// error variant, and as its success variant either a pointer to the payload or
// nothing if the payload is empty.
template <typename FidlMethod>
struct BaseWireResultStorage<FidlMethod, std::enable_if_t<FidlMethod::kHasApplicationError, void>> {
  BaseWireResultStorage() = delete;

  using Type = std::optional<WireResultUnwrapType<FidlMethod>>;
};

template <typename FidlMethod>
using BaseWireResultStorageType = typename BaseWireResultStorage<FidlMethod>::Type;
}  // namespace internal

// |BaseWireResult| is a set of templates that provide a base class for
// |WireResult| to use.
template <typename FidlMethod, typename Enable = void>
class BaseWireResult;

// Template variant for methods without a response body, and therefore nothing
// to |Unwrap|:
// - Methods without response, that is one-way methods.
// - Methods with a header-only response.
template <typename FidlMethod>
class BaseWireResult<
    FidlMethod, std::enable_if_t<!FidlMethod::kHasResponse || !FidlMethod::kHasResponseBody, void>>
    : public ::fidl::Status {
 protected:
  explicit BaseWireResult(const ::fidl::Status& status) : ::fidl::Status(status) {}

  BaseWireResult() = default;
  BaseWireResult(BaseWireResult&&) noexcept = default;
  BaseWireResult(BaseWireResult&) = delete;
  BaseWireResult& operator=(BaseWireResult&&) noexcept = default;
  BaseWireResult& operator=(const BaseWireResult&) = delete;
  ~BaseWireResult() = default;
};

// Template variant for flexible methods with a response body but which don't
// need |Unwrap| accessors. This means a flexible method which doesn't use error
// syntax and has an empty payload: `flexible Foo() -> (struct {});`.
template <typename FidlMethod>
class BaseWireResult<FidlMethod,
                     std::enable_if_t<FidlMethod::kHasResponse && FidlMethod::kHasResponseBody &&
                                          !MethodHasUnwrapAccessors<FidlMethod>(),
                                      void>> : public ::fidl::Status {
 protected:
  explicit BaseWireResult(const ::fidl::Status& status) : ::fidl::Status(status) {}

  BaseWireResult() = default;
  BaseWireResult(BaseWireResult&&) noexcept = default;
  BaseWireResult(BaseWireResult&) = delete;
  BaseWireResult& operator=(BaseWireResult&&) noexcept = default;
  BaseWireResult& operator=(const BaseWireResult&) = delete;
  ~BaseWireResult() = default;

  // This method should be called from the generated |WireResult| or
  // |WireUnownedResult| after checking that the decoding status is |ok()|. If
  // the method uses a Result union, this method will handle unpacking the
  // result.
  //
  // For this template, which is only for methods with no application error and
  // an empty payload, this method just checks if transport_err is used (if the
  // method is flexible) and changes the status to
  // |Status::UnknownMethod()| if necessary.
  void ExtractValueFromDecoded(::fidl::WireResponse<FidlMethod>* raw_response) {
    static_assert(!FidlMethod::kHasNonEmptyPayload);
    static_assert(FidlMethod::kHasTransportError);
    // For a flexible method, we need to check whether the result is success
    // or transport_err.
    if (raw_response->result.is_transport_err()) {
      switch (raw_response->result.transport_err()) {
        case ::fidl::internal::TransportErr::kUnknownMethod:
          SetStatus(::fidl::Status::UnknownMethod());
          return;
      }
      ZX_PANIC("Unknown transport_err");
    } else {
      ZX_ASSERT_MSG(raw_response->result.is_response(), "Unknown FIDL result union variant");
    }
  }
};

// Template variant for method which need |Unwrap| and related accessors. This
// means a method which has a non-empty payload or uses error syntax.
template <typename FidlMethod>
class BaseWireResult<
    FidlMethod,
    std::enable_if_t<FidlMethod::kHasResponse && MethodHasUnwrapAccessors<FidlMethod>(), void>>
    : public ::fidl::Status {
 public:
  // Gets a pointer to the result of the method call, if it succeeded. For a
  // method without error syntax, this is the return type of the method. For a
  // method with error syntax, this is a |fit::result| of the error type and a
  // pointer to the return type (if the return type is not empty).
  WireResultUnwrapType<FidlMethod>* Unwrap() {
    if constexpr (FidlMethod::kHasApplicationError) {
      return &result_.value();
    } else {
      static_assert(FidlMethod::kHasNonEmptyPayload);
      ZX_ASSERT(ok());
      return result_;
    }
  }
  // Gets a pointer to the result of the method call, if it succeeded. For a
  // method without error syntax, this is the return type of the method. For a
  // method with error syntax, this is a |fit::result| of the error type and a
  // pointer to the return type (if the return type is not empty).
  const WireResultUnwrapType<FidlMethod>* Unwrap() const {
    if constexpr (FidlMethod::kHasApplicationError) {
      return &result_.value();
    } else {
      static_assert(FidlMethod::kHasNonEmptyPayload);
      ZX_ASSERT(ok());
      return result_;
    }
  }

  // Gets a reference to the result of the method call, if it succeeded. For a
  // method without error syntax, this is the return type of the method. For a
  // method with error syntax, this is a |fit::result| of the error type and a
  // pointer to the return type (if the return type is not empty).
  WireResultUnwrapType<FidlMethod>& value() { return *Unwrap(); }

  // Gets a reference to the result of the method call, if it succeeded. For a
  // method without error syntax, this is the return type of the method. For a
  // method with error syntax, this is a |fit::result| of the error type and a
  // pointer to the return type (if the return type is not empty).
  const WireResultUnwrapType<FidlMethod>& value() const { return *Unwrap(); }

  WireResultUnwrapType<FidlMethod>* operator->() { return &value(); }
  const WireResultUnwrapType<FidlMethod>* operator->() const { return &value(); }

  WireResultUnwrapType<FidlMethod>& operator*() { return value(); }
  const WireResultUnwrapType<FidlMethod>& operator*() const { return value(); }

 protected:
  explicit BaseWireResult(const ::fidl::Status& status) : ::fidl::Status(status) {}

  BaseWireResult() = default;
  BaseWireResult(BaseWireResult&&) noexcept = default;
  BaseWireResult(BaseWireResult&) = delete;
  BaseWireResult& operator=(BaseWireResult&&) noexcept = default;
  BaseWireResult& operator=(const BaseWireResult&) = delete;
  ~BaseWireResult() = default;

  // This method should be called from the generated |WireResult| or
  // |WireUnownedResult| after checking that the decoding status is |ok()|. If
  // the method uses a Result union, this method will handle unpacking the
  // result.
  //
  // For this template, which covers any method which has a non-empty payload
  // or an application error, this method handles both checking the transport
  // error (if the method is flexible) and the error (if the method uses error
  // syntax) and putting a reference to the content into the |result_| as needed
  // for the |Unwrap| accessors.
  void ExtractValueFromDecoded(::fidl::WireResponse<FidlMethod>* raw_response) {
    if constexpr (FidlMethod::kHasApplicationError && FidlMethod::kHasTransportError) {
      if (raw_response->result.is_transport_err()) {
        SetStatus(::fidl::Status::UnknownMethod());
      } else if (raw_response->result.is_err()) {
        result_ = fit::error(raw_response->result.err());
      } else {
        ZX_ASSERT_MSG(raw_response->result.is_response(), "Unknown FIDL result union variant");
        if constexpr (FidlMethod::kHasNonEmptyPayload) {
          result_ = fit::ok(&(raw_response->result.response()));
        } else {
          result_ = fit::ok();
        }
      }
    } else if constexpr (FidlMethod::kHasTransportError) {
      if (raw_response->result.is_transport_err()) {
        SetStatus(::fidl::Status::UnknownMethod());
      } else {
        // Result must be non-empty, because if there is no application error
        // and the result is empty, we would use the template without the Unwrap
        // accessors.
        static_assert(FidlMethod::kHasNonEmptyPayload);
        ZX_ASSERT_MSG(raw_response->result.is_response(), "Unknown FIDL result union variant");
        result_ = &(raw_response->result.response());
      }
    } else if constexpr (FidlMethod::kHasApplicationError) {
      if (raw_response->result.is_err()) {
        result_ = fit::error(raw_response->result.err());
      } else {
        ZX_ASSERT_MSG(raw_response->result.is_response(), "Unknown FIDL result union variant");
        if constexpr (FidlMethod::kHasNonEmptyPayload) {
          result_ = fit::ok(&(raw_response->result.response()));
        } else {
          result_ = fit::ok();
        }
      }
    } else {
      // If the method doesn't use a result union, the result is just the whole
      // transactional response body.
      result_ = raw_response;
    }
  }

 private:
  internal::BaseWireResultStorageType<FidlMethod> result_;
};

}  // namespace fidl

#endif  // LIB_FIDL_CPP_WIRE_INCLUDE_LIB_FIDL_CPP_WIRE_BASE_WIRE_RESULT_H_
