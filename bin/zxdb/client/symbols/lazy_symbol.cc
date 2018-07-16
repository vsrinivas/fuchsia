// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/client/symbols/lazy_symbol.h"

#include "garnet/bin/zxdb/client/symbols/symbol.h"
#include "garnet/bin/zxdb/client/symbols/symbol_factory.h"

namespace zxdb {

LazySymbol::LazySymbol() = default;
LazySymbol::LazySymbol(fxl::RefPtr<SymbolFactory> factory,
                       void* factory_data_ptr, uint32_t factory_data_offset)
    : factory_(std::move(factory)),
      factory_data_ptr_(factory_data_ptr),
      factory_data_offset_(factory_data_offset) {}
LazySymbol::~LazySymbol() = default;

const Symbol* LazySymbol::Get() const {
  FXL_DCHECK(is_valid());

  if (!symbol_.get()) {
    if (is_valid())
      symbol_ = factory_->CreateSymbol(factory_data_ptr_, factory_data_offset_);
    else
      symbol_ = fxl::MakeRefCounted<Symbol>();
  }
  return symbol_.get();
}

}  // namespace zxdb
