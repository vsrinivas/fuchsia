// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/expr/resolve_array.h"

#include "src/developer/debug/zxdb/common/err.h"
#include "src/developer/debug/zxdb/expr/eval_context.h"
#include "src/developer/debug/zxdb/expr/expr_value.h"
#include "src/developer/debug/zxdb/expr/pretty_type.h"
#include "src/developer/debug/zxdb/expr/pretty_type_manager.h"
#include "src/developer/debug/zxdb/symbols/arch.h"
#include "src/developer/debug/zxdb/symbols/array_type.h"
#include "src/developer/debug/zxdb/symbols/modified_type.h"
#include "src/developer/debug/zxdb/symbols/symbol_data_provider.h"
#include "src/developer/debug/zxdb/symbols/type.h"

namespace zxdb {

namespace {

Err ResolveStaticArray(const ExprValue& array, const ArrayType* array_type, size_t begin_index,
                       size_t end_index, std::vector<ExprValue>* result) {
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

    std::vector<uint8_t> item_data(&data[begin_offset], &data[begin_offset] + type_size);
    result->emplace_back(RefPtrTo(value_type), std::move(item_data),
                         array.source().GetOffsetInto(begin_offset));
  }
  return Err();
}

// Handles the "Foo*" case.
void ResolvePointerArray(fxl::RefPtr<EvalContext> eval_context, const ExprValue& array,
                         const ModifiedType* ptr_type, size_t begin_index, size_t end_index,
                         fit::callback<void(const Err&, std::vector<ExprValue>)> cb) {
  const Type* abstract_value_type = ptr_type->modified().Get()->AsType();
  if (!abstract_value_type) {
    cb(Err("Bad type information."), std::vector<ExprValue>());
    return;
  }
  fxl::RefPtr<Type> value_type = eval_context->GetConcreteType(abstract_value_type);

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

  eval_context->GetDataProvider()->GetMemoryAsync(
      begin_address, end_address - begin_address,
      [value_type, begin_address, count = end_index - begin_index, cb = std::move(cb)](
          const Err& err, std::vector<uint8_t> data) mutable {
        if (err.has_error()) {
          cb(err, std::vector<ExprValue>());
          return;
        }
        // Convert returned raw memory to ExprValues.
        uint32_t type_size = value_type->byte_size();
        std::vector<ExprValue> result;
        result.reserve(count);
        for (size_t i = 0; i < count; i++) {
          size_t begin_offset = i * type_size;
          if (begin_offset + type_size > data.size())
            break;  // Ran out of data, leave remaining results uninitialized.

          std::vector<uint8_t> item_data(&data[begin_offset], &data[begin_offset + type_size]);
          result.emplace_back(value_type, std::move(item_data),
                              ExprValueSource(begin_address + begin_offset));
        }
        cb(Err(), std::move(result));
      });
}

// Backend for the single-item and async multiple item array resolution.
//
// Returns true if the callback was consumed. This means the item was an array or pointer that can
// be handled (in which case the callback will have been either issued or will be pending). False
// means that the item wasn't an array and the callback was not used.
bool DoResolveArray(fxl::RefPtr<EvalContext> eval_context, const ExprValue& array,
                    size_t begin_index, size_t end_index,
                    fit::callback<void(const Err&, std::vector<ExprValue>)> cb) {
  if (!array.type()) {
    cb(Err("No type information."), std::vector<ExprValue>());
    return true;  // Invalid but the callback was issued.
  }

  fxl::RefPtr<Type> concrete = eval_context->GetConcreteType(array.type());
  if (const ArrayType* array_type = concrete->AsArrayType()) {
    std::vector<ExprValue> result;
    Err err = ResolveStaticArray(array, array_type, begin_index, end_index, &result);
    cb(err, result);
    return true;
  } else if (const ModifiedType* modified_type = concrete->AsModifiedType()) {
    if (modified_type->tag() == DwarfTag::kPointerType) {
      ResolvePointerArray(eval_context, array, modified_type, begin_index, end_index,
                          std::move(cb));
      return true;
    }
  }

  // Not an array.
  return false;
}

}  // namespace

Err ResolveArray(fxl::RefPtr<EvalContext> eval_context, const ExprValue& array, size_t begin_index,
                 size_t end_index, std::vector<ExprValue>* result) {
  if (!array.type())
    return Err("No type information.");

  fxl::RefPtr<Type> concrete = eval_context->GetConcreteType(array.type());
  if (const ArrayType* array_type = concrete->AsArrayType()) {
    return ResolveStaticArray(array, array_type, begin_index, end_index, result);
  }
  return Err("Can't dereference a non-array type.");
}

void ResolveArray(fxl::RefPtr<EvalContext> eval_context, const ExprValue& array, size_t begin_index,
                  size_t end_index, fit::callback<void(const Err&, std::vector<ExprValue>)> cb) {
  if (!DoResolveArray(eval_context, array, begin_index, end_index, std::move(cb)))
    cb(Err("Can't dereference a non-pointer or array type."), std::vector<ExprValue>());
}

void ResolveArrayItem(fxl::RefPtr<EvalContext> eval_context, const ExprValue& array, size_t index,
                      fit::callback<void(const Err&, ExprValue)> cb) {
  // This callback might possibly be bound to the regular array access function and we won't know
  // if it was needed until the function returns. We want to try regular resolution first to avoid
  // over-triggering pretty-printing if something is configured incorrectly. This case is not
  // performance sensitive so this extra allocation doesn't matter much.
  auto shared_cb = std::make_shared<fit::callback<void(const Err&, ExprValue)>>(std::move(cb));

  // Try a regular access first.
  if (DoResolveArray(eval_context, array, index, index + 1,
                     [shared_cb](const Err& err, std::vector<ExprValue> result_vect) {
                       if (err.has_error())
                         (*shared_cb)(err, ExprValue());
                       else if (result_vect.empty())  // Short read.
                         (*shared_cb)(Err("Invalid array index."), ExprValue());
                       else
                         (*shared_cb)(Err(), std::move(result_vect[0]));
                     }))
    return;  // Handled by the regular array access.

  // Check for pretty types that support array access, shared_cb is our responsibility.
  if (const PrettyType* pretty = eval_context->GetPrettyTypeManager().GetForType(array.type())) {
    if (auto array_access = pretty->GetArrayAccess())
      return array_access(eval_context, array, index, std::move(*shared_cb));
  }
}

}  // namespace zxdb
