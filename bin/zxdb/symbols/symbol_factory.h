// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <inttypes.h>
#include <memory>

#include "lib/fxl/memory/ref_counted.h"

namespace zxdb {

class Symbol;

// This class converts the information from a LazySymbol to a real Symbol.
//
// Having this class be reference counted also solves the problem of lifetimes.
// The module may get unloaded, and with it the symbol information. It's too
// error-prone to require Symbols not be cached since they will be very common.
//
// This class allows each LazySymbol to have one reference-counted pointer
// (relatively lightweight) to the factory. The factory can then have one
// (expensive) weak pointer to the underlying module symbols. When the module
// is unloaded, the factory may still be around but it will return empty types.
//
// Last, this class allows types to be mocked without requiring that the
// full and complex Symbol interface be virutal and duplicated.
class SymbolFactory : public fxl::RefCountedThreadSafe<SymbolFactory> {
 public:
  SymbolFactory() = default;
  virtual ~SymbolFactory() = default;

  // This function should never return null. To indicate failure, return a new
  // default-constructed Symbol object.
  virtual fxl::RefPtr<Symbol> CreateSymbol(void* data_ptr, uint32_t offset) = 0;
};

}  // namespace zxdb
