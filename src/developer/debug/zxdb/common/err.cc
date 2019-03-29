// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "err.h"

#include <stdarg.h>

#include "src/lib/fxl/strings/string_printf.h"

namespace zxdb {

Err::Err() = default;

Err::Err(ErrType type, const std::string& msg) : type_(type), msg_(msg) {}

Err::Err(const std::string& msg) : type_(ErrType::kGeneral), msg_(msg) {}

Err::Err(const char* fmt, ...) : type_(ErrType::kGeneral) {
  va_list ap;
  va_start(ap, fmt);
  msg_ = fxl::StringVPrintf(fmt, ap);
  va_end(ap);
}

Err::~Err() = default;

bool Err::operator==(const Err& other) const {
  return type_ == other.type_ && msg_ == other.msg_;
}

}  // namespace zxdb
