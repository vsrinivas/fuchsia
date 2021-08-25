// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_FUCHSIA_CXX_RESULT_H_
#define SRC_LIB_FUCHSIA_CXX_RESULT_H_

#include <lib/fitx/result.h>
#include <zircon/status.h>

#include <cstdint>
#include <string>

// Header generated from Rust cxxbridge declarations. Include dir added by build template.
#include "src/lib/fuchsia-cxx/src/result.rs.h"

namespace rust::zx {

namespace impl {  // Internal, not part of the API.

struct StatusAndMessage {
  zx_status_t status;
  std::string message;

  explicit StatusAndMessage(ffi::Result r) : status(r.status), message(std::move(r.message)) {
    // This should only be used to hold error results. Use make_result to create an appropriate
    // Result from any ffi::Result.
    if (status == ZX_OK) {
      __builtin_abort();
    }
  }
};

}  // namespace impl

// Import supporting types and functions from fitx.
using fitx::error;
using fitx::ok;
using fitx::success;

// Base type.
template <typename... Ts>
class Result;

// Result is a zx::status with an added error message, accessed through error_message(). The
// interface is otherwise identical to zx::status.
//
// Typically you'll want to use make_result() below to turn an ffi::Result into the appropriate
// ok/error rust::zx::Result.
// Implicitly constructible from error<ffi::Result> through conversion to
// error<impl::StatusAndMessage>.
//
// Specialization of Result for returning a single value.
template <typename T>
class [[nodiscard]] Result<T> : public ::fitx::result<impl::StatusAndMessage, T> {
  using base = ::fitx::result<impl::StatusAndMessage, T>;

 public:
  using base::base;

  // NOLINTNEXTLINE(google-explicit-constructor)
  Result(error<ffi::Result> error) : base{error} {}

  // Returns the underlying error or ZX_OK if not in the error state. This
  // accessor simplifies interfacing with code that uses zx_status_t directly.
  constexpr zx_status_t status_value() const {
    return this->is_error() ? base::error_value().status : ZX_OK;
  }

  // Returns the string representation of the status value.
  const char* status_string() const { return zx_status_get_string(status_value()); }

  // Accessor for the underlying error message. If is_error(), this will contain a string
  // representation of the Rust error (from std::fmt::Display) that occurred on the other side of
  // the FFI.
  //
  // May only be called when the result contains an error.
  constexpr const std::string& error_message() const { return base::error_value().message; }
};

// Specialization of Result for empty value type.
template <>
class [[nodiscard]] Result<> : public ::fitx::result<impl::StatusAndMessage> {
  using base = ::fitx::result<impl::StatusAndMessage>;

 public:
  using base::base;

  // NOLINTNEXTLINE(google-explicit-constructor)
  Result(error<ffi::Result> error) : base{error} {}

  constexpr zx_status_t status_value() const {
    return this->is_error() ? base::error_value().status : ZX_OK;
  }
  const char* status_string() const { return zx_status_get_string(status_value()); }
  constexpr const std::string& error_message() const { return base::error_value().message; }
};

// Utility to make a Result<> from an ffi::Result.
Result<> make_result(ffi::Result result);

}  // namespace rust::zx

#endif  // SRC_LIB_FUCHSIA_CXX_RESULT_H_
