// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/expr/resolve_pointer.h"

#include "garnet/bin/zxdb/common/err.h"
#include "garnet/bin/zxdb/expr/expr_value.h"
#include "garnet/bin/zxdb/symbols/symbol_data_provider.h"
#include "garnet/bin/zxdb/symbols/type.h"
#include "garnet/bin/zxdb/symbols/type_utils.h"
#include "lib/fxl/strings/string_printf.h"

namespace zxdb {

void ResolvePointer(fxl::RefPtr<SymbolDataProvider> data_provider,
                    uint64_t address, fxl::RefPtr<Type> type,
                    std::function<void(const Err&, ExprValue)> cb) {
  if (!type) {
    cb(Err("Missing pointer type."), ExprValue());
    return;
  }

  uint32_t type_size = type->byte_size();
  data_provider->GetMemoryAsync(address, type_size, [
    type = std::move(type), address, cb = std::move(cb)
  ](const Err& err, std::vector<uint8_t> data) {
    if (err.has_error()) {
      cb(err, ExprValue());
    } else if (data.size() != type->byte_size()) {
      // Short read, memory is invalid.
      cb(Err(fxl::StringPrintf("Invalid pointer 0x%" PRIx64, address)),
         ExprValue());
    } else {
      cb(Err(),
         ExprValue(std::move(type), std::move(data), ExprValueSource(address)));
    }
  });
}

void ResolvePointer(fxl::RefPtr<SymbolDataProvider> data_provider,
                    const ExprValue& pointer,
                    std::function<void(const Err&, ExprValue)> cb) {
  const Type* pointed_to = nullptr;
  Err err = GetPointedToType(pointer.type(), &pointed_to);
  if (err.has_error()) {
    cb(err, ExprValue());
    return;
  }

  err = pointer.EnsureSizeIs(sizeof(uint64_t));
  if (err.has_error()) {
    cb(err, ExprValue());
    return;
  }

  ResolvePointer(std::move(data_provider), pointer.GetAs<uint64_t>(),
                 fxl::RefPtr<Type>(const_cast<Type*>(pointed_to)),
                 std::move(cb));
}

}  // namespace zxdb
