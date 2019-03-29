// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/expr/resolve_array.h"

#include "src/developer/debug/zxdb/common/err.h"
#include "src/developer/debug/zxdb/expr/expr_value.h"
#include "src/developer/debug/zxdb/symbols/arch.h"
#include "src/developer/debug/zxdb/symbols/array_type.h"
#include "src/developer/debug/zxdb/symbols/modified_type.h"
#include "src/developer/debug/zxdb/symbols/symbol_data_provider.h"
#include "src/developer/debug/zxdb/symbols/type.h"

namespace zxdb {

namespace {

Err ResolveStaticArray(const ExprValue& array, const ArrayType* array_type,
                       size_t begin_index, size_t end_index,
                       std::vector<ExprValue>* result) {
  const std::vector<uint8_t>& data = array.data();
  if (data.size() < array_type->byte_size()) {
    return Err(
        "Array data (%zu bytes) is too small for the expected size "
        "(%u bytes).",
        data.size(), array_type->byte_size());
  }

  const Type* value_type = array_type->value_type();
  uint32_t type_size = value_type->byte_size();

  result->reserve(end_index - begin_index);
  for (size_t i = begin_index; i < end_index; i++) {
    size_t begin_offset = i * type_size;
    if (begin_offset + type_size > data.size())
      break;

    std::vector<uint8_t> item_data(&data[begin_offset],
                                   &data[begin_offset] + type_size);
    result->emplace_back(fxl::RefPtr<Type>(const_cast<Type*>(value_type)),
                         std::move(item_data),
                         array.source().GetOffsetInto(begin_offset));
  }
  return Err();
}

// Handles the "Foo*" case.
void ResolvePointerArray(
    fxl::RefPtr<SymbolDataProvider> data_provider, const ExprValue& array,
    const ModifiedType* ptr_type, size_t begin_index, size_t end_index,
    std::function<void(const Err&, std::vector<ExprValue>)> cb) {
  const Type* value_type = ptr_type->modified().Get()->AsType();
  if (!value_type) {
    cb(Err("Bad type information."), std::vector<ExprValue>());
    return;
  }
  value_type = value_type->GetConcreteType();

  // The address is stored in the contents of the array value.
  Err err = array.EnsureSizeIs(kTargetPointerSize);
  if (err.has_error()) {
    cb(err, std::vector<ExprValue>());
    return;
  }
  TargetPointer base_address = array.GetAs<TargetPointer>();

  uint32_t type_size = value_type->byte_size();
  TargetPointer begin_address = base_address + type_size * begin_index;
  TargetPointer end_address = base_address + type_size * end_index;

  data_provider->GetMemoryAsync(
      begin_address, end_address - begin_address,
      [type = fxl::RefPtr<Type>(const_cast<Type*>(value_type)), begin_address,
       count = end_index - begin_index,
       cb = std::move(cb)](const Err& err, std::vector<uint8_t> data) {
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

}  // namespace

Err ResolveArray(const ExprValue& array, size_t begin_index, size_t end_index,
                 std::vector<ExprValue>* result) {
  if (!array.type())
    return Err("No type information.");

  const Type* concrete = array.type()->GetConcreteType();
  if (const ArrayType* array_type = concrete->AsArrayType()) {
    return ResolveStaticArray(array, array_type, begin_index, end_index,
                              result);
  }
  return Err("Can't dereference a non-array type.");
}

void ResolveArray(fxl::RefPtr<SymbolDataProvider> data_provider,
                  const ExprValue& array, size_t begin_index, size_t end_index,
                  std::function<void(const Err&, std::vector<ExprValue>)> cb) {
  if (!array.type()) {
    cb(Err("No type information."), std::vector<ExprValue>());
    return;
  }

  const Type* concrete = array.type()->GetConcreteType();
  if (const ArrayType* array_type = concrete->AsArrayType()) {
    std::vector<ExprValue> result;
    Err err =
        ResolveStaticArray(array, array_type, begin_index, end_index, &result);
    cb(err, result);
    return;
  } else if (const ModifiedType* modified_type = concrete->AsModifiedType()) {
    if (modified_type->tag() == DwarfTag::kPointerType) {
      return ResolvePointerArray(data_provider, array, modified_type,
                                 begin_index, end_index, std::move(cb));
    }
  }
  cb(Err("Can't dereference a non-pointer or array type."),
     std::vector<ExprValue>());
}

}  // namespace zxdb
