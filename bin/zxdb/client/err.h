// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <string>

namespace zxdb {

enum class ErrType {
  kNone,
  kGeneral,  // Unspecified error type.
  kInput,  // Some problem getting input from the user (parse error, etc.).
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

 private:
  ErrType type_ = ErrType::kNone;
  std::string msg_;
};

}  // namespace zxdb
