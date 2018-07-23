// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdint.h>

#include "garnet/bin/zxdb/client/symbols/file_line.h"
#include "garnet/bin/zxdb/client/symbols/lazy_symbol.h"

namespace zxdb {

class CodeBlock;
class Function;

// Represents all the symbol information for a code location.
class Location {
 public:
  // A location can be invalid (has no address), can have an address that we
  // haven't tried to symbolize, and a symbolized address. The latter two
  // states allow symbolizing on demand without having additional types.
  //
  // The "symbolized" state doesn't necessarily mean there are symbols, it
  // just means we tried to symbolize it.
  enum class State { kInvalid, kAddress, kSymbolized };

  Location();
  Location(State state, uint64_t address);
  Location(uint64_t address, FileLine&& file_line, int column,
           const LazySymbol& function = LazySymbol());
  ~Location();

  bool is_valid() const { return state_ != State::kInvalid; }

  // The different between "symbolized" and "has_symbols" is that the former
  // means we tried to symbolize it, and the latter means we actually succeeded.
  bool is_symbolized() const { return state_ == State::kSymbolized; }
  bool has_symbols() const { return file_line_.is_valid(); }

  uint64_t address() const { return address_; }
  const FileLine& file_line() const { return file_line_; }
  int column() const { return column_; }

  // The function associated with this address, if any. This will be the
  // most specific inline or regular function covering the given address.
  //
  // This isn't necessarily valid, even if the State == kSymbolized. It could
  // be the symbol table indicates file/line info for this address but could
  // lack a function record for it.
  const LazySymbol& function() const { return function_; }

  // Offsets the code addresses in this by adding an amount. This is used to
  // convert module-relative addresses to global ones by adding the module
  // load address.
  void AddAddressOffset(uint64_t offset);

 private:
  State state_ = State::kInvalid;
  uint64_t address_ = 0;
  FileLine file_line_;
  int column_ = 0;
  LazySymbol function_;
};

}  // namespace zxdb
