// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/symbols/location.h"

#include <inttypes.h>

#include "src/developer/debug/zxdb/symbols/function.h"
#include "src/developer/debug/zxdb/symbols/symbol.h"
#include "src/lib/fxl/strings/string_printf.h"

namespace zxdb {

Location::Location() = default;
Location::Location(State state, uint64_t address) : state_(state), address_(address) {}

Location::Location(uint64_t address, FileLine file_line, int column,
                   const SymbolContext& symbol_context, LazySymbol symbol)
    : state_(State::kSymbolized),
      address_(address),
      file_line_(std::move(file_line)),
      column_(column),
      symbol_(std::move(symbol)),
      symbol_context_(symbol_context) {}

Location::Location(const SymbolContext& symbol_context, LazySymbol symbol)
    : state_(State::kUnlocatedVariable),
      symbol_(std::move(symbol)),
      symbol_context_(symbol_context) {}

Location::~Location() = default;

void Location::AddAddressOffset(uint64_t offset) {
  if (!is_valid())
    return;
  address_ += offset;
}

bool Location::EqualsIgnoringSymbol(const Location& other) const {
  return state_ == other.state_ && address_ == other.address_ && file_line_ == other.file_line_ &&
         column_ == other.column_ && symbol_context_ == other.symbol_context_;
}

// static
const char* Location::StateToString(State state) {
  switch (state) {
    case State::kInvalid:
      return "invalid";
    case State::kAddress:
      return "address";
    case State::kSymbolized:
      return "symbolized";
    case State::kUnlocatedVariable:
      return "unlocated_variable";
  }
  return "CORRUPTED_STATE";
}

std::string Location::GetDebugString() const {
  return fxl::StringPrintf("Location(state=%s, address=0x%" PRIx64 " file=%s, line=%d, column=%d)",
                           StateToString(state_), address_, file_line_.file().c_str(),
                           file_line_.line(), column_);
}

}  // namespace zxdb
