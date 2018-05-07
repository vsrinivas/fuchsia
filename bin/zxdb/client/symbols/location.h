// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "garnet/bin/zxdb/client/symbols/symbol.h"

namespace zxdb {

// While a Symbol is an abstract name for some stuff in source code, a
// Location refers to a specific place in memory, that may or may not have a
// corresponding symbol.
class Location {
 public:
  Location() = default;
  explicit Location(uint64_t address) : address_(address) {}
  Location(uint64_t address, Symbol symbol)
      : address_(address), symbol_(std::move(symbol)) {}

  uint64_t address() const { return address_; }
  void set_address(uint64_t a) { address_ = a; }

  const Symbol& symbol() const { return symbol_; }
  void set_symbol(Symbol s) { symbol_ = std::move(s); }

 private:
  uint64_t address_ = 0;
  Symbol symbol_;
};

}  // namespace zxdb
