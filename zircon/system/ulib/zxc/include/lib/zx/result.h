// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_ZX_STATUS_H_
#define LIB_ZX_STATUS_H_

#include <lib/fit/internal/compiler.h>
#include <lib/fit/result.h>
#include <zircon/errors.h>
#include <zircon/types.h>

namespace zx {

// Simplified result type for returning either a zx_status_t error or zero/one values. See
// lib/fit/result.h for an explanation of the general result type.
//
// To make a zx::result:
//
//   zx::ok()                    // For success on zx::result<>.
//   zx::ok(foo)                 // For success on zx::result<Foo>.
//
//   zx::error(ZX_ERR_NO_MEMORY) // For failure.
//
// General functions that can always be called:
//
//   bool is_ok()
//   bool is_error()
//   zx_status_t status_value()  // Returns the error value or ZX_OK on success.
//   const char* status_string() // String representation of the error (Fuchsia only).
//   T value_or(default_value)   // Returns value on success, or default on failure.
//
// Available only when is_ok():
//
//   T& value()                  // Accesses the value.
//   T&& value()                 // Moves the value.
//   T& operator*()              // Accesses the value.
//   T&& operator*()             // Moves the value.
//   T* operator->()             // Accesses the value.
//   success<T> take_value()     // Generates a zx::success() which can be implicitly converted to
//                               // another fit::result with the same "success" type.
//
// Available only when is_error():
//
//   zx_status_t error_value()   // Error code. See also status_value() which is always usable.
//   error<E> take_error()       // Generates a zx::error() which can be implicitly converted to a
//                               // zx::result with another "success" type (or zx::result<>).
//
// Examples:
//
//   zx::result<fbl::RefPtr<Node>> MakeNode(Args...) {
//     fbl::AllocChecker alloc_checker;
//     auto* node_ptr = new (&alloc_checker) Node(Args...);
//     if (!alloc_checker.check()) {
//       return zx::error(ZX_ERR_NO_MEMORY);
//     }
//     return zx::ok(fbl::RefPtr{node_ptr});
//   }
//
//   zx::result<> AddNewNode(Tree* tree, Args...) {
//     auto status = MakeNode(Args...));
//     if (status.is_ok() {
//       tree->AddNode(std::move(status.value()));
//       return zx::ok();
//     }
//     return status.take_error();
//   }
//

// Import supporting types and functions from fit.
using fit::as_error;
using fit::error;
using fit::failed;
using fit::ok;
using fit::success;

// Base type.
template <typename... Ts>
class result;

// Specialization of status for returning a single value.
template <typename T>
class LIB_FIT_NODISCARD result<T> : public ::fit::result<zx_status_t, T> {
  using base = ::fit::result<zx_status_t, T>;

 public:
  using base::base;

  // Implicit conversion from error<zx_status_t>.
  constexpr result(error<zx_status_t> error) : base{error} {
    // It is invalid to pass ZX_OK as an error state. Use zx::ok() or
    // zx::success to indicate success. See zx::make_result for forwarding
    // errors from code that uses zx_status_t.
    if (base::error_value() == ZX_OK) {
      __builtin_abort();
    }
  }

  // Returns the underlying error or ZX_OK if not in the error state. This
  // accessor simplifies interfacing with code that uses zx_status_t directly.
  constexpr zx_status_t status_value() const {
    return this->is_error() ? base::error_value() : ZX_OK;
  }

#if defined(__Fuchsia__)
  // Returns the string representation of the status value.
  const char* status_string() const;
#endif  // defined(__Fuchsia__)
};

// Specialization of status for empty value type.
template <>
class LIB_FIT_NODISCARD result<> : public ::fit::result<zx_status_t> {
  using base = ::fit::result<zx_status_t>;

 public:
  using base::base;

  // Implicit conversion from error<zx_status_t>.
  constexpr result(error<zx_status_t> error) : base{error} {
    // It is invalid to pass ZX_OK as an error state. Use zx::ok() or
    // zx::success to indicate success. See zx::make_result for forwarding
    // errors from code that uses zx_status_t.
    if (base::error_value() == ZX_OK) {
      __builtin_abort();
    }
  }

  // Returns the underlying error or ZX_OK if not in the error state. This
  // accessor simplifies interfacing with code that uses zx_status_t directly.
  constexpr zx_status_t status_value() const {
    return this->is_error() ? base::error_value() : ZX_OK;
  }

#if defined(__Fuchsia__)
  // Returns the string representation of the status value.
  const char* status_string() const;
#endif  // defined(__Fuchsia__)
};

// Simplified alias of zx::error<zx_status_t>.
using error_result = error<zx_status_t>;

// Utility to make a status-only zx::result<> from a zx_status_t error.
//
// A status-only zx::result<> is one with an empty value set. It may contain
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
//   zx::result<> ConsumeValues(std::array<Value, kSize>* values) {
//     if (values == nullptr) {
//       return zx::error_result(ZX_ERR_INVALID_ARGS);
//     }
//     return zx::make_result(ConsumeValues(values->data(), values->length()));
//   }
//
constexpr result<> make_result(zx_status_t status) {
  if (status == ZX_OK) {
    return ok();
  }
  return error_result{status};
}

// Utility to make a zx::result<T> from a zx_status_t and T.
//
// Depending on |status|, the resulting zx::result<T> will be either
// zx::ok(value) or zx::error(status).
//
// Example:
//
//   // Legacy method returning zx_status_t.
//   zx_status_t ComputeValue(Value* value);
//
//   // Newer method that interops with the legacy method.
//   zx::result<Value> ComputeValue() {
//     Value value;
//     return zx::make_result(ComputeValue(&value), value);
//   }
//
// Note, because the order of evaluation of function arguments is unspecified,
// it's critical that the second parameter to zx::make_result (`value`) is
// passed by reference and *not* passed by value. If it were passed by value,
// then its value may be bound before the first argument has been evaluated
// (i.e. before ComputeValue was even called!).
//
// Furthermore, pass by reference is not always sufficient to prevent subtle
// order of evaluation bugs. Consider the following buggy code:
//
//   // Legacy method returning zx_status_t.
//   zx_status_t ComputeValue(std::unique_ptr<Value>* value);
//
//   // BUGGY CODE
//   zx::result<Value> ComputeValue() {
//     std::unique_ptr<Value> value;
//     return zx::make_result(ComputeValue(&value), *value);  // <--- BUGGY CODE
//   }
//
// Depending on the compiler, the code above might work or my dereference a null
// std::unique_ptr. When in doubt, use a local variable.
//
template <typename T>
constexpr result<std::remove_reference_t<T>> make_result(zx_status_t status, T&& value) {
  if (status == ZX_OK) {
    return ok(std::forward<T>(value));
  }
  return error_result{status};
}

#if defined(__Fuchsia__)
template <typename T>
const char* result<T>::status_string() const {
  return make_result(status_value()).status_string();
}
#endif  // defined(__Fuchsia__)

}  // namespace zx

#endif  // LIB_ZX_STATUS_H_
