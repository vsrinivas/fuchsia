// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdint.h>
#include <string>

#include "garnet/bin/zxdb/client/symbols/file_line.h"

namespace zxdb {

// Used for specifying the input location for things like "run to here" and
// breakpoints. For these use-cases the user might specify the location in a
// variety of forms.
//
// See also the "Location" object which is an output location that provides
// all information (address, symbols, etc.) for some state.
//
// For the symbol and file name options, the symbol name and file name
// must match exactly the full version of that from the symbol system.
// The caller will need to have resolve file names with the symbol system
// prior to setting.
struct InputLocation {
  enum class Type { kNone, kLine, kSymbol, kAddress };

  InputLocation() = default;
  explicit InputLocation(FileLine file_line)
      : type(Type::kLine), line(std::move(file_line)) {}
  explicit InputLocation(std::string symbol)
      : type(Type::kSymbol), symbol(std::move(symbol)) {}
  explicit InputLocation(uint64_t address)
      : type(Type::kAddress), address(address) {}

  Type type = Type::kNone;

  FileLine line;       // Valid when type == kLine;
  std::string symbol;  // Valid when type == kSymbol;
  uint64_t address;    // Valid when type == kAddress;
};

}  // namespace zxdb
