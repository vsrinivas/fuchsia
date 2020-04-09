// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "encode.h"

#include <fuchsia/diagnostics/stream/cpp/fidl.h>
#include <lib/zx/clock.h>
#include <zircon/status.h>

#include <iostream>
#include <vector>

namespace streams {
namespace internal {

void write_string(const std::string& str, std::vector<uint8_t>* out) {
  out->insert(out->end(), std::begin(str), std::end(str));
}

void write_signed_int(int signed_int, std::vector<uint8_t>* out) {
  // Write Signed Int
}

void write_unsigned_int(unsigned int unsigned_int, std::vector<uint8_t>* out) {
  // Write Unigned Int
}

void write_float(float f, std::vector<uint8_t>* out) {
  // Write float
}

zx_status_t log_value(const fuchsia::diagnostics::stream::Value& arg,
                      std::vector<uint8_t>* out) {
  switch (arg.Which()) {
    case fuchsia::diagnostics::stream::Value::Tag::kSignedInt: {
      write_signed_int(arg.signed_int(), out);
      break;
    }

    case fuchsia::diagnostics::stream::Value::Tag::kUnsignedInt: {
      write_unsigned_int(arg.unsigned_int(), out);
      break;
    }

    case fuchsia::diagnostics::stream::Value::Tag::kFloating: {
      write_float(arg.floating(), out);
      break;
    }

    case fuchsia::diagnostics::stream::Value::Tag::kText: {
      write_string(arg.text(), out);
      break;
    }

    case fuchsia::diagnostics::stream::Value::Tag::kUnknown: {
    }

    default: {
      // Error
    }
  }
  return ZX_OK;
}

zx_status_t log_argument(const fuchsia::diagnostics::stream::Argument& arg,
                         std::vector<uint8_t>* out) {
  // Write the arg
  write_string(arg.name, out);
  // Then write the value
  log_value(arg.value, out);
  return ZX_OK;
}

}  // namespace internal
zx_status_t log_record(const fuchsia::diagnostics::stream::Record& record,
                       std::vector<uint8_t>* out) {
  size_t idx = out->size();
  // Add record header
  zx_time_t time = record.timestamp;
  out->resize(idx + sizeof(time));
  std::memcpy(out->data() + idx, &time, sizeof(time));
  // Add the arguments
  for (unsigned long i = 0; i < record.arguments.size(); i++) {
    internal::log_argument(record.arguments[i], out);
  }
  return ZX_OK;
}

}  // namespace streams
