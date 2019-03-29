// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/expr/resolve_ptr_ref.h"

#include "src/lib/fxl/strings/string_printf.h"
#include "src/developer/debug/zxdb/common/err.h"
#include "src/developer/debug/zxdb/expr/expr_value.h"
#include "src/developer/debug/zxdb/symbols/arch.h"
#include "src/developer/debug/zxdb/symbols/modified_type.h"
#include "src/developer/debug/zxdb/symbols/symbol_data_provider.h"
#include "src/developer/debug/zxdb/symbols/type.h"
#include "src/developer/debug/zxdb/symbols/type_utils.h"

namespace zxdb {

namespace {

// Extracts the value from the ExprValue, assuming it's a pointer. If not,
// return the error, otherwise fill in *pointer_value.
Err GetPointerValue(const ExprValue& value, TargetPointer* pointer_value) {
  Err err = value.EnsureSizeIs(kTargetPointerSize);
  if (err.has_error())
    return err;
  *pointer_value = value.GetAs<TargetPointer>();
  return Err();
}

}  // namespace

void ResolvePointer(fxl::RefPtr<SymbolDataProvider> data_provider,
                    uint64_t address, fxl::RefPtr<Type> type,
                    std::function<void(const Err&, ExprValue)> cb) {
  if (!type) {
    cb(Err("Missing pointer type."), ExprValue());
    return;
  }

  uint32_t type_size = type->byte_size();
  data_provider->GetMemoryAsync(
      address, type_size,
      [type = std::move(type), address, cb = std::move(cb)](
          const Err& err, std::vector<uint8_t> data) {
        if (err.has_error()) {
          cb(err, ExprValue());
        } else if (data.size() != type->byte_size()) {
          // Short read, memory is invalid.
          cb(Err(fxl::StringPrintf("Invalid pointer 0x%" PRIx64, address)),
             ExprValue());
        } else {
          cb(Err(), ExprValue(std::move(type), std::move(data),
                              ExprValueSource(address)));
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

  TargetPointer pointer_value = 0;
  err = GetPointerValue(pointer, &pointer_value);
  if (err.has_error()) {
    cb(err, ExprValue());
  } else {
    ResolvePointer(std::move(data_provider), pointer_value,
                   fxl::RefPtr<Type>(const_cast<Type*>(pointed_to)),
                   std::move(cb));
  }
}

void EnsureResolveReference(fxl::RefPtr<SymbolDataProvider> data_provider,
                            ExprValue value,
                            std::function<void(const Err&, ExprValue)> cb) {
  Type* type = value.type();
  if (!type) {
    // Untyped input, pass the value forward and let the callback handle the
    // problem.
    cb(Err(), std::move(value));
    return;
  }

  const Type* concrete = type->GetConcreteType();  // Strip "const", etc.
  if (concrete->tag() != DwarfTag::kReferenceType &&
      concrete->tag() != DwarfTag::kRvalueReferenceType) {
    // Not a reference, nothing to do.
    cb(Err(), std::move(value));
    return;
  }
  // The symbol provider should have created the right object type.
  const ModifiedType* reference = concrete->AsModifiedType();
  FXL_DCHECK(reference);
  const Type* underlying_type = reference->modified().Get()->AsType();

  // The value will be the address for reference types.
  TargetPointer pointer_value = 0;
  Err err = GetPointerValue(value, &pointer_value);
  if (err.has_error()) {
    cb(err, ExprValue());
  } else {
    ResolvePointer(std::move(data_provider), pointer_value,
                   fxl::RefPtr<Type>(const_cast<Type*>(underlying_type)),
                   std::move(cb));
  }
}

}  // namespace zxdb
