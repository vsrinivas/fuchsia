// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_SYMBOLS_INPUT_LOCATION_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_SYMBOLS_INPUT_LOCATION_H_

#include <stdint.h>

#include <string>
#include <vector>

#include "src/developer/debug/zxdb/symbols/file_line.h"
#include "src/developer/debug/zxdb/symbols/identifier.h"

namespace zxdb {

// Used for specifying the input location for things like "run to here" and breakpoints. For these
// use-cases the user might specify the location in a variety of forms.
//
// See also the "Location" object which is an output location that provides all information
// (address, symbols, etc.) for some state.
//
// For the symbol name and file name options, the name name must match exactly the full version of
// that from the symbol system. The caller will need to have resolve file names with the
// symbol system prior to setting.
struct InputLocation {
  enum class Type {
    kNone,     // Default initialized, unusable for lookup.
    kLine,     // File/line query.
    kName,     // Identifier-based query (names of symbols like functions).
    kAddress,  // Address in a running process.
  };

  InputLocation() = default;
  explicit InputLocation(FileLine file_line) : type(Type::kLine), line(std::move(file_line)) {}
  explicit InputLocation(Identifier name) : type(Type::kName), name(std::move(name)) {}
  explicit InputLocation(uint64_t address) : type(Type::kAddress), address(address) {}

  // Converts the input location type to a string. This is intended to be used in error messages.
  static const char* TypeToString(Type type);

  bool operator==(const InputLocation& other) const;
  bool operator!=(const InputLocation& other) const { return !operator==(other); }

  Type type = Type::kNone;

  // Valid when type == kLine;
  FileLine line;

  // Valid when type == kName.
  Identifier name;

  // Valid when type == kAddress;
  uint64_t address;
};

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_SYMBOLS_INPUT_LOCATION_H_
