// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "err.h"

#include <lib/syslog/cpp/macros.h>
#include <stdarg.h>

#include "src/developer/debug/shared/status.h"
#include "src/lib/fxl/strings/string_printf.h"

namespace zxdb {

Err::Err() = default;

Err::Err(ErrType type, const std::string& msg) : type_(type), msg_(msg) {}

Err::Err(const std::string& msg) : type_(ErrType::kGeneral), msg_(msg) {}

Err::Err(const debug::Status& debug_status) {
  switch (debug_status.type()) {
    case debug::Status::kSuccess:
      type_ = ErrType::kNone;
      break;
    case debug::Status::kGenericError:
    case debug::Status::kPlatformError:
      // We currently don't preserve the platform error code, but assume that it's been
      // stringified into a reasonable message.
      type_ = ErrType::kGeneral;
      msg_ = debug_status.message();
      break;
    case debug::Status::kNotSupported:
      type_ = ErrType::kNotSupported;
      msg_ = debug_status.message();
      break;
    case debug::Status::kNotFound:
      type_ = ErrType::kNotFound;
      msg_ = debug_status.message();
      break;
    case debug::Status::kAlreadyExists:
      type_ = ErrType::kAlreadyExists;
      msg_ = debug_status.message();
      break;
    case debug::Status::kNoResources:
      type_ = ErrType::kNoResources;
      msg_ = debug_status.message();
      break;
    case debug::Status::kLast:
      FX_NOTREACHED();
      type_ = ErrType::kGeneral;
      break;
  }
}

Err::Err(const char* fmt, ...) : type_(ErrType::kGeneral) {
  va_list ap;
  va_start(ap, fmt);
  msg_ = fxl::StringVPrintf(fmt, ap);
  va_end(ap);
}

Err::~Err() = default;

bool Err::operator==(const Err& other) const { return type_ == other.type_ && msg_ == other.msg_; }

}  // namespace zxdb
