// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "err.h"

namespace zxdb {

Err::Err() = default;

Err::Err(ErrType type, const std::string& msg) : type_(type), msg_(msg) {}

Err::Err(const std::string& msg) : type_(ErrType::kGeneral), msg_(msg) {}

Err::~Err() = default;

bool Err::operator==(const Err& other) const {
  return type_ == other.type_ && msg_ == other.msg_;
}

}  // namespace zxdb
