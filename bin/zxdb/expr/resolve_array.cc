// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/expr/resolve_array.h"

#include "garnet/bin/zxdb/client/symbols/symbol_data_provider.h"
#include "garnet/bin/zxdb/client/symbols/type.h"
#include "garnet/bin/zxdb/common/err.h"
#include "garnet/bin/zxdb/expr/expr_value.h"

namespace zxdb {

void ResolveValueArray(
    fxl::RefPtr<SymbolDataProvider> data_provider, const Type* value_type,
    uint64_t base_address, size_t begin_index, size_t end_index,
    std::function<void(const Err&, std::vector<ExprValue>)> cb) {
  uint32_t type_size = value_type->byte_size();
  uint64_t begin_address = base_address + type_size * begin_index;
  uint64_t end_address = base_address + type_size * end_index;

  data_provider->GetMemoryAsync(begin_address, end_address - begin_address, [
    type = fxl::RefPtr<Type>(const_cast<Type*>(value_type)), begin_address,
    count = end_index - begin_index, cb = std::move(cb)
  ](const Err& err, std::vector<uint8_t> data) {
    if (err.has_error()) {
      cb(err, std::vector<ExprValue>());
      return;
    }

    // Convert returned raw memory to ExprValues.
    uint32_t type_size = type->byte_size();
    std::vector<ExprValue> result;
    result.reserve(count);
    for (size_t i = 0; i < count; i++) {
      size_t begin_offset = i * type_size;
      if (begin_offset + type_size > data.size())
        break;  // Ran out of data, leave remaining results uninitialized.

      std::vector<uint8_t> item_data(&data[begin_offset],
                                     &data[begin_offset + type_size]);
      result.emplace_back(type, std::move(item_data),
                          ExprValueSource(begin_address + begin_offset));
    }
    cb(Err(), std::move(result));
  });
}

void ResolveByteArray(
    fxl::RefPtr<SymbolDataProvider> data_provider, uint64_t base_address,
    size_t begin_index, size_t end_index,
    std::function<void(const Err&, std::vector<uint8_t>)> cb) {
  uint64_t begin_address = base_address + begin_index;
  uint64_t end_address = base_address + end_index;
  data_provider->GetMemoryAsync(begin_address, end_address - begin_address,
                                std::move(cb));
}

}  // namespace zxdb
