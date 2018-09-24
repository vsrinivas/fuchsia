// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdint.h>

namespace zxdb {

// Addressess in symbols are interpreted in terms of relative offsets from the
// beginning of the module. When a module is loaded into memory, it has a
// physical address, and all symbol addresses need to be offset to convert
// back and forth between physical and symbolic addresses.
//
// This class holds the load information for converting between these two
// address types.
class SymbolContext {
 public:
  // Creates a symbol context has no offset. Using it will return addresses
  // relative to the module. This can be useful for testing and for resolving
  // which symbols might be available even when no program is running.
  static SymbolContext ForRelativeAddresses() { return SymbolContext(); }

  explicit SymbolContext(uint64_t load_address) : load_address_(load_address) {}

  uint64_t RelativeToAbsolute(uint64_t relative) const {
    return load_address_ + relative;
  }
  uint64_t AbsoluteToRelative(uint64_t absolute) const {
    return absolute - load_address_;
  }

 private:
  // Use ForRelativeAddresses() to create a context with no load address.
  SymbolContext() = default;

  uint64_t load_address_ = 0;
};

}  // namespace zxdb
