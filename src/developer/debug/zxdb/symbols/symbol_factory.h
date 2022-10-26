// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_SYMBOLS_SYMBOL_FACTORY_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_SYMBOLS_SYMBOL_FACTORY_H_

#include <inttypes.h>

#include <memory>

#include "src/lib/fxl/memory/ref_counted.h"

namespace zxdb {

class Symbol;

// This class converts the information from a LazySymbol to a real Symbol.
//
// Having this class be reference counted also solves the problem of lifetimes.  The module may get
// unloaded, and with it the symbol information. It's too error-prone to require Symbols not be
// cached since they will be very common.
//
// This class allows each LazySymbol to have one reference-counted pointer (relatively lightweight)
// to the factory. The factory can then have one (expensive) weak pointer to the underlying module
// symbols. When the module is unloaded, the factory may still be around but it will return empty
// types.
//
// Last, this class allows types to be mocked without requiring that the full and complex Symbol
// interface be virtual and duplicated.
class SymbolFactory : public fxl::RefCountedThreadSafe<SymbolFactory> {
 public:
  // This function should never return null. To indicate failure, return a new default-constructed
  // Symbol object.
  virtual fxl::RefPtr<Symbol> CreateSymbol(uint64_t die_offset) const = 0;

 protected:
  FRIEND_REF_COUNTED_THREAD_SAFE(SymbolFactory);

  SymbolFactory() = default;
  virtual ~SymbolFactory() = default;
};

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_SYMBOLS_SYMBOL_FACTORY_H_
