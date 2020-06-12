// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TOOLS_FIDLCAT_LIB_DECODER_H_
#define TOOLS_FIDLCAT_LIB_DECODER_H_

#include <sstream>
#include <string>
#include <string_view>

#include "src/developer/debug/zxdb/symbols/location.h"
#include "tools/fidlcat/lib/type_decoder.h"

namespace fidlcat {

class Location;

class DecoderError {
 public:
  enum class Type { kNone, kCantReadMemory, kUnknownArchitecture };

  DecoderError() = default;

  Type type() const { return type_; }
  std::string message() const { return message_.str(); }

  std::stringstream& Set(Type type) {
    if (type_ == Type::kNone) {
      type_ = type;
    } else {
      message_ << '\n';
    }
    return message_;
  }

 private:
  Type type_ = Type::kNone;
  std::stringstream message_;
};

void DisplayStackFrame(const std::vector<zxdb::Location>& caller_locations,
                       fidl_codec::PrettyPrinter& printer);

// Copies the stack frame into fidlcat data.
void CopyStackFrame(const std::vector<zxdb::Location>& caller_locations,
                    std::vector<Location>* locations);

}  // namespace fidlcat

#endif  // TOOLS_FIDLCAT_LIB_DECODER_H_
