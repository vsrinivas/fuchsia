// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_SYMBOLS_LOCATION_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_SYMBOLS_LOCATION_H_

#include <stdint.h>

#include "src/developer/debug/zxdb/symbols/file_line.h"
#include "src/developer/debug/zxdb/symbols/lazy_symbol.h"
#include "src/developer/debug/zxdb/symbols/symbol_context.h"

namespace zxdb {

class CodeBlock;
class Function;

// Represents all the symbol information for a code location.
class Location {
 public:
  // A location can be invalid (has no address), can have an address that we haven't tried to
  // symbolize, and a symbolized address. The latter two states allow symbolizing on demand without
  // having additional types.
  //
  // The "symbolized" state doesn't necessarily mean there are symbols, it just means we tried to
  // symbolize it.
  enum class State {
    // There is no address or data for this location.
    kInvalid,

    // There is an address for this location but we haven't tried to symbolize it.
    kAddress,

    // There is an address for the location and we tried to symbolize it. This doesn't mean that
    // symbolization succeeded, so the symbol() and file/line could still be empty.
    kSymbolized,

    // The symbol corresponds to a Variable, but its location has not been computed so it will have
    // a null address.
    //
    // Some global variables actually need to be evaluated asynchronously based on the current CPU
    // state. For example, TLS values are located relative to the CPU register that indicates the
    // TLS base. When resolving a symbolic name, we can encounter these which can't be evaluated in
    // the global context of the symbol system.
    //
    // Currently these aren't handled in most places. But if a caller is in a position to evaluate
    // this it can fill out the address from the symbol.
    kUnlocatedVariable,
  };

  Location();
  Location(State state, uint64_t address);

  // Symbolized location.
  Location(uint64_t address, FileLine file_line, int column, const SymbolContext& symbol_context,
           LazySymbol symbol = LazySymbol());

  // Unlocated variable.
  Location(const SymbolContext& symbol_context, LazySymbol symbol);

  ~Location();

  bool is_valid() const { return state_ != State::kInvalid; }

  // The different between "symbolized" and "has_symbols" is that the former means we tried to
  // symbolize it, and the latter means we actually succeeded to symbolize EITHER the line or the
  // function. One or the other could be missing, however.
  bool is_symbolized() const { return state_ == State::kSymbolized; }
  bool has_symbols() const { return file_line_.is_valid() || symbol_; }

  // The absolute address of this location.
  uint64_t address() const { return address_; }

  const FileLine& file_line() const { return file_line_; }
  int column() const { return column_; }

  // The symbol associated with this address, if any. In the case of code this will normally be a
  // Function. It will not be the code block inside the function (code wanting lexical blocks can
  // look inside the function's children as needed). It could also be a variable symbol
  // corresponding to a global or static variable or an ELF symbol.
  //
  // When looking up code locations from the symbol system, this will be the most specific FUNCTION
  // covering the code in question (the innermost inlined function if there is one). But Locations
  // may be generated (e.g. by the stack unwinder) for any of the other inlined functions that may
  // cover the same address.
  //
  // A function can have different scopes inside of it. To get the current lexical scope inside the
  // function, use GetMostSpecificChild() on it.
  //
  // This isn't necessarily valid, even if the State == kSymbolized. It could be the symbol table
  // indicates file/line info for this address but could lack a function record for it.
  const LazySymbol& symbol() const { return symbol_; }

  // Symbolized locations will have a valid symbol context for converting addresses.
  const SymbolContext& symbol_context() const { return symbol_context_; }

  // Offsets the code addresses in this by adding an amount. This is used to convert module-relative
  // addresses to global ones by adding the module load address.
  void AddAddressOffset(uint64_t offset);

  // Returns if this location is the same as the other one, ignoring the symbol() object. Comparing
  // symbol objects is dicy because the same symbol can result in a different object depending on
  // how it is found or whether it was re-queried.
  //
  // This function is primarily used for tests, in which case comparing object pointer equality
  // might be good enough. For non-tests, one might compare symbols by name.
  bool EqualsIgnoringSymbol(const Location& other) const;

  // Returns a string version of the State enum for debugging purposes.
  static const char* StateToString(State state);

  // Returns a description of this Location for debugging purposes.
  std::string GetDebugString() const;

 private:
  State state_ = State::kInvalid;
  uint64_t address_ = 0;
  FileLine file_line_;
  int column_ = 0;
  LazySymbol symbol_;
  SymbolContext symbol_context_ = SymbolContext::ForRelativeAddresses();
};

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_SYMBOLS_LOCATION_H_
