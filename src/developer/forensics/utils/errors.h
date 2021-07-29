// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FORENSICS_UTILS_ERRORS_H_
#define SRC_DEVELOPER_FORENSICS_UTILS_ERRORS_H_

#include <lib/syslog/cpp/macros.h>

#include <string>
#include <type_traits>
#include <variant>

namespace forensics {

// Defines common errors that occur throughout //src/developer/feedback.
enum class Error {
  kNotSet,
  // TODO(fxbug.dev/49922): Remove kDefault. This value is temporary to allow the enum to be used
  // without specifying the exact error that occurred.
  kDefault,
  kLogicError,
  kTimeout,
  kConnectionError,
  kAsyncTaskPostFailure,
  kMissingValue,
  kBadValue,
  kFileReadFailure,
  kFileWriteFailure,
  // Custom errors code that can be interpreted in different ways by different components.
  kCustom,
};

template <typename T>
class ErrorOr {
 public:
  ErrorOr(T value) : data_(std::move(value)) {}
  ErrorOr(enum Error error) : data_(error) {}

  // Allow construction from a type U iff U is convertible to T, but not the otherway around.
  template <typename U,
            std::enable_if_t<std::conjunction_v<std::is_convertible<U, T>,
                                                std::negation<std::is_convertible<T, U>>>,
                             bool> = true>
  ErrorOr(U value) : data_(T(std::move(value))) {}

  bool HasValue() const { return data_.index() == 0; }

  const T& Value() const {
    FX_CHECK(HasValue());
    return std::get<T>(data_);
  }

  enum Error Error() const {
    FX_CHECK(!HasValue());
    return std::get<enum Error>(data_);
  }

  bool operator==(const ErrorOr& other) const { return data_ == other.data_; }
  bool operator!=(const ErrorOr& other) const { return !(*this == other); }

 private:
  std::variant<T, enum Error> data_;
};

// Provide a string representation of  |error|.
inline std::string ToString(Error error) {
  switch (error) {
    case Error::kNotSet:
      return "Error::kNotSet";
    case Error::kDefault:
      return "Error::kDefault";
    case Error::kLogicError:
      return "Error::kLogicError";
    case Error::kTimeout:
      return "Error::kTimeout";
    case Error::kConnectionError:
      return "Error::kConnectionError";
    case Error::kAsyncTaskPostFailure:
      return "Error::kAsyncTaskPostFailure";
    case Error::kMissingValue:
      return "Error::kMissingValue";
    case Error::kBadValue:
      return "Error::kBadValue";
    case Error::kFileReadFailure:
      return "Error::kFileReadFailure";
    case Error::kFileWriteFailure:
      return "Error::kFileWriteFailure";
    case Error::kCustom:
      return "Error::kCustom";
  }
}

}  // namespace forensics

#endif  // SRC_DEVELOPER_FORENSICS_UTILS_ERRORS_H_
