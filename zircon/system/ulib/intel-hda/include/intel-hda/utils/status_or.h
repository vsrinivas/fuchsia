// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_ULIB_INTEL_HDA_INCLUDE_INTEL_HDA_UTILS_STATUS_OR_H_
#define ZIRCON_SYSTEM_ULIB_INTEL_HDA_INCLUDE_INTEL_HDA_UTILS_STATUS_OR_H_

#include <zircon/assert.h>
#include <zircon/errors.h>

#include <type_traits>
#include <utility>
#include <variant>

#include <intel-hda/utils/status.h>

namespace audio::intel_hda {

template <typename T>
class [[nodiscard]] StatusOr {
 public:
  static_assert(!std::is_same<T, std::remove_cv<Status>::type>::value,
                "StatusOr<Status> not supported.");

  // Create a StatusOr object with error ZX_ERR_INTERNAL.
  explicit StatusOr() : value_(std::in_place_index_t<0>{}, ZX_ERR_INTERNAL) {}

  // Create a StatusOr object with with given Status.
  //
  // It is an error to produce a StatusOr<> with an "Ok" status.
  //
  // Allow implicit conversion from Status objects.
  StatusOr(const Status& err)  // NOLINT(google-explicit-constructor)
      : value_(std::in_place_index_t<0>{}, err) {
    ZX_DEBUG_ASSERT(!err.ok());
  }
  StatusOr(Status && err)  // NOLINT(google-explicit-constructor)
      : value_(std::in_place_index_t<0>{}, std::move(err)) {
    ZX_DEBUG_ASSERT(!std::get<0>(value_).ok());
  }

  // Create a StatusOr object with with given value T.
  //
  // Allow implicit conversion from T.
  StatusOr(const T& val)  // NOLINT(google-explicit-constructor)
      : value_(std::in_place_index_t<1>{}, val) {}
  StatusOr(T && val)  // NOLINT(google-explicit-constructor)
      : value_(std::in_place_index_t<1>{}, std::move(val)) {}

  // Move / copy constructors
  StatusOr(const StatusOr& other) = default;
  StatusOr(StatusOr && other) = default;
  StatusOr& operator=(const StatusOr& other) = default;
  StatusOr& operator=(StatusOr&& other) = default;

  // Return true if we have a value.
  [[nodiscard]] bool ok() const { return value_.index() == 1; }

  // Return the status code if we have an error, or OkStatus() if we are ok.
  [[nodiscard]] const Status& status() const {
    if (ok()) {
      return kOkStatus;
    }
    return std::get<0>(value_);
  }

  // Return the value, or abort execution if we have an error.
  [[nodiscard]] const T& ValueOrDie() const {
    if (unlikely(!ok())) {
      ZX_PANIC("Attempted to get value of StatusOr in error state.");
    }
    return std::get<1>(value_);
  }

  // Move the value, or abort execution if we have an error.
  [[nodiscard]] T ConsumeValueOrDie() {
    if (unlikely(!ok())) {
      ZX_PANIC("Attempted to get value of StatusOr in error state.");
    }
    return std::move(std::get<1>(value_));
  }

 private:
  // Singleton "OkStatus()" instance returned by "status()" when StatusOr<>
  // has a non-error value.
  static inline const Status kOkStatus = OkStatus();

  std::variant<Status, T> value_;
};

}  // namespace audio::intel_hda

#endif  // ZIRCON_SYSTEM_ULIB_INTEL-HDA_INCLUDE_INTEL-HDA_UTILS_STATUS_OR_H_
