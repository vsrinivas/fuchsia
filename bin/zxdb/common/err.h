// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <string>

namespace zxdb {

enum class ErrType {
  kNone,

  // Unspecified error type.
  kGeneral,

  // The operation was explicitly canceled.
  kCanceled,

  // There is no connection to the debug agent and this operation can't be
  // completed.
  kNoConnection,

  // Data was corrupted between us and the debug agent.
  kCorruptMessage,

  // An invalid client API call.
  kClientApi,

  // Some problem getting input from the user (parse error, etc.).
  kInput,
};

class Err {
 public:
  // Indicates no error.
  Err();

  // Indicates an error of the given type with an optional error message.
  Err(ErrType type, const std::string& msg = std::string());

  // Produces a "general" error with the given message.
  Err(const std::string& msg);

  ~Err();

  bool has_error() const { return type_ != ErrType::kNone; }
  bool ok() const { return type_ == ErrType::kNone; }

  ErrType type() const { return type_; }
  const std::string& msg() const { return msg_; }

  // Equality operator is provided for tests.
  bool operator==(const Err& other) const;

 private:
  ErrType type_ = ErrType::kNone;
  std::string msg_;
};

}  // namespace zxdb
