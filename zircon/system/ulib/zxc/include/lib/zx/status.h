// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_ZX_STATUS_H_
#define LIB_ZX_STATUS_H_

#include <lib/fitx/result.h>
#include <zircon/errors.h>
#include <zircon/types.h>

namespace zx {

// Simplified result type for returning either a zx_status_t error or zero/one
// values. See lib/fitx/result.h for an explanation of the general result type.
//
// Examples:
//
//   zx::status<fbl::RefPtr<Node>> MakeNode(Args...) {
//     fbl::AllocChecker alloc_checker;
//     auto* node_ptr = new (&alloc_checker) Node(Args...);
//     if (!alloc_checker.check()) {
//       return zx::error(ZX_ERR_NO_MEMORY);
//     }
//     return zx::ok(fbl::RefPtr{node_ptr});
//   }
//
//   zx::status<> AddNewNode(Tree* tree, Args...) {
//     auto status = MakeNode(Args...));
//     if (status.has_value() {
//       tree->AddNode(std::move(node_ref));
//       return zx::ok();
//     }
//     return status.take_error();
//   }
//

// Import supporting types and functions from fitx.
using fitx::ok;
using fitx::error;
using fitx::failed;
using fitx::success;
using fitx::as_error;

// Base type.
template <typename... Ts>
class status;

// Specialization of status for returning a single value.
template <typename T>
class status<T> : public ::fitx::result<zx_status_t, T> {
  using base = ::fitx::result<zx_status_t, T>;

 public:
  using base::base;

  // Implicit conversion from error<zx_status_t>.
  constexpr status(error<zx_status_t> error) : base{error} {
    // It is invalid to pass ZX_OK as an error state. Use zx::ok() or
    // zx::success to indicate success. See zx::make_status for forwarding
    // errors from code that uses zx_status_t.
    if (base::error_value() == ZX_OK) {
      __builtin_abort();
    }
  }

  // Returns the underlying error or ZX_OK if not in the error state. This
  // accessor simplifies interfacing with code that uses zx_status_t directly.
  constexpr zx_status_t status_value() const {
    return this->has_error() ? base::error_value() : ZX_OK;
  }
};

// Specialization of status for empty value type.
template <>
class status<> : public ::fitx::result<zx_status_t> {
  using base = ::fitx::result<zx_status_t>;

 public:
  using base::base;

  // Implicit conversion from error<zx_status_t>.
  constexpr status(error<zx_status_t> error) : base{error} {
    // It is invalid to pass ZX_OK as an error state. Use zx::ok() or
    // zx::success to indicate success. See zx::make_status for forwarding
    // errors from code that uses zx_status_t.
    if (base::error_value() == ZX_OK) {
      __builtin_abort();
    }
  }

  // Returns the underlying error or ZX_OK if not in the error state. This
  // accessor simplifies interfacing with code that uses zx_status_t directly.
  constexpr zx_status_t status_value() const {
    return this->has_error() ? base::error_value() : ZX_OK;
  }
};

// Simplified alias of zx::error<zx_status_t>.
using error_status = error<zx_status_t>;

// Utility to make a status-only zx::status<> from a zx_status_t error.
//
// A status-only zx::status<> is one with an empty value set. It may contain
// either a status value that represents the error (i.e. not ZX_OK) or a
// valueless success state. This utility automatically handles the distinction
// to make interop with older code easier.
//
// Example usage:
//
//   // Legacy method returning zx_status_t.
//   zx_status_t ConsumeValues(Value* values, size_t length);
//
//   // Newer method that interops with the legacy method.
//   zx::status<> ConsumeValues(std::array<Value, kSize>* values) {
//     if (values == nullptr) {
//       return zx::error_status(ZX_ERR_INVALID_ARGS);
//     }
//     return zx::make_status(ConsumeValues(values->data(), values->length()));
//   }
//
constexpr status<> make_status(zx_status_t status) {
  if (status == ZX_OK) {
    return ok();
  }
  return error_status{status};
}

}  // namespace zx

#endif  // LIB_ZX_STATUS_H_
