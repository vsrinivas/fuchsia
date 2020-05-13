// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/symbols/input_location.h"

#include <lib/syslog/cpp/macros.h>

namespace zxdb {

const char* InputLocation::TypeToString(Type type) {
  switch (type) {
    case Type::kLine:
      return "file/line";
    case Type::kName:
      return "name";
    case Type::kAddress:
      return "address";
    case Type::kNone:
      return "<no location type>";
  }

  FX_NOTREACHED();
}

bool InputLocation::operator==(const InputLocation& other) const {
  if (type != other.type)
    return false;

  switch (type) {
    case Type::kLine:
      return line == other.line;
    case Type::kName:
      return name == other.name;
    case Type::kAddress:
      return address == other.address;
    case Type::kNone:
      return true;
  }

  FX_NOTREACHED();
}

}  // namespace zxdb
