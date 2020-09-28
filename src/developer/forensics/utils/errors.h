// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FORENSICS_UTILS_ERRORS_H_
#define SRC_DEVELOPER_FORENSICS_UTILS_ERRORS_H_

#include <lib/syslog/cpp/macros.h>

#include <string>
#include <variant>

namespace forensics {

// Defines common errors that occur throughout //src/developer/feedback.
enum class Error {
  kNotSet,
  // TODO(fxbug.dev/49922): Remove kDefault. This value is temporary to allow the enum to be used without
  // specifying the exact error that occurred.
  kDefault,
  kLogicError,
  kTimeout,
  kConnectionError,
  kAsyncTaskPostFailure,
  kMissingValue,
  kBadValue,
  kFileReadFailure,
  kFileWriteFailure,
};

template <typename T>
class ErrorOr {
 public:
  ErrorOr(T value) : data_(std::move(value)) {}
  ErrorOr(enum Error error) : data_(error) {}

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
  }
}

}  // namespace forensics

#endif  // SRC_DEVELOPER_FORENSICS_UTILS_ERRORS_H_
