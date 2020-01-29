// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_SYMBOLS_SYMBOL_CONTEXT_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_SYMBOLS_SYMBOL_CONTEXT_H_

#include <stdint.h>

#include "src/developer/debug/zxdb/common/address_ranges.h"

namespace zxdb {

// Addresses in symbols are interpreted in terms of relative offsets from the beginning of the
// module. When a module is loaded into memory, it has a physical address, and all symbol addresses
// need to be offset to convert back and forth between physical and symbolic addresses.
//
// This class holds the load information for converting between these two address types.
class SymbolContext {
 public:
  // Creates a symbol context has no offset. Using it will return addresses relative to the module.
  // This can be useful for testing and for resolving which symbols might be available even when no
  // program is running.
  static SymbolContext ForRelativeAddresses() { return SymbolContext(); }

  explicit SymbolContext(uint64_t load_address) : load_address_(load_address) {}

  // Returns true if this is a relative symbol context. Relative contexts do not have a real
  // loaded module associated with them and addresses will be unchanged between relative and
  // absolute.
  bool is_relative() const { return load_address_ == 0; }

  uint64_t load_address() const { return load_address_; }

  bool operator==(const SymbolContext& other) const { return load_address_ == other.load_address_; }
  bool operator!=(const SymbolContext& other) const { return load_address_ != other.load_address_; }

  // Address conversion.
  uint64_t RelativeToAbsolute(uint64_t relative) const { return load_address_ + relative; }
  uint64_t AbsoluteToRelative(uint64_t absolute) const { return absolute - load_address_; }

  // AddressRange conversion.
  AddressRange RelativeToAbsolute(const AddressRange& relative) const;
  AddressRange AbsoluteToRelative(const AddressRange& absolute) const;

  // AddressRanges conversion.
  AddressRanges RelativeToAbsolute(const AddressRanges& relative) const;
  AddressRanges AbsoluteToRelative(const AddressRanges& absolute) const;

 private:
  // Use ForRelativeAddresses() to create a context with no load address.
  SymbolContext() = default;

  uint64_t load_address_ = 0;
};

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_SYMBOLS_SYMBOL_CONTEXT_H_
