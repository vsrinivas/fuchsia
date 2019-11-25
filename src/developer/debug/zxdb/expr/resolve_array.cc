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

// Handles the "Foo[4]" case.
ErrOrValueVector ResolveStaticArray(const ExprValue& array, const ArrayType* array_type,
                                    size_t begin_index, size_t end_index) {
  const std::vector<uint8_t>& data = array.data();
  if (data.size() < array_type->byte_size()) {
    return Err(
        "Array data (%zu bytes) is too small for the expected size "
        "(%u bytes).",
        data.size(), array_type->byte_size());
  }

  const Type* value_type = array_type->value_type();
  uint32_t type_size = value_type->byte_size();

  std::vector<ExprValue> result;
  result.reserve(end_index - begin_index);
  for (size_t i = begin_index; i < end_index; i++) {
    size_t begin_offset = i * type_size;
    if (begin_offset + type_size > data.size())
      break;

    ExprValueSource source = array.source();
    if (source.type() == ExprValueSource::Type::kMemory) {
      source = source.GetOffsetInto(begin_offset);
    } else if (source.type() == ExprValueSource::Type::kRegister) {
      // Vector register, compute the bit shifts for this subset. This assumes little-endian
      // so we can compute the bit shifts to write to the register from the left.
      source = ExprValueSource(source.register_id(), type_size * 8,
                               source.bit_shift() + (begin_offset * 8));
    }
    // else keep as original temporary/constant source.

    std::vector<uint8_t> item_data(&data[begin_offset], &data[begin_offset] + type_size);
    result.emplace_back(RefPtrTo(value_type), std::move(item_data), source);
  }
  return result;
}

// Handles the "Foo*" case.
void ResolvePointerArray(const fxl::RefPtr<EvalContext>& eval_context, const ExprValue& array,
                         const ModifiedType* ptr_type, size_t begin_index, size_t end_index,
                         fit::callback<void(ErrOrValueVector)> cb) {
  const Type* abstract_value_type = ptr_type->modified().Get()->AsType();
  if (!abstract_value_type)
    return cb(Err("Bad type information."));
  fxl::RefPtr<Type> value_type = eval_context->GetConcreteType(abstract_value_type);

  // The address is stored in the contents of the array value.
  Err err = array.EnsureSizeIs(kTargetPointerSize);
  if (err.has_error())
    return cb(err);
  TargetPointer base_address = array.GetAs<TargetPointer>();

  uint32_t type_size = value_type->byte_size();
  TargetPointer begin_address = base_address + type_size * begin_index;
  TargetPointer end_address = base_address + type_size * end_index;

  eval_context->GetDataProvider()->GetMemoryAsync(
      begin_address, end_address - begin_address,
      [value_type, begin_address, count = end_index - begin_index, cb = std::move(cb)](
          const Err& err, std::vector<uint8_t> data) mutable {
        if (err.has_error())
          return cb(err);

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
        cb(std::move(result));
      });
}

// Backend for the single-item and async multiple item array resolution.
//
// Returns true if the callback was consumed. This means the item was an array or pointer that can
// be handled (in which case the callback will have been either issued or will be pending). False
// means that the item wasn't an array and the callback was not used.
bool DoResolveArray(const fxl::RefPtr<EvalContext>& eval_context, const ExprValue& array,
                    size_t begin_index, size_t end_index,
                    fit::callback<void(ErrOrValueVector)> cb) {
  if (!array.type()) {
    cb(Err("No type information."));
    return true;  // Invalid but the callback was issued.
  }

  fxl::RefPtr<Type> concrete = eval_context->GetConcreteType(array.type());
  if (const ArrayType* array_type = concrete->AsArrayType()) {
    std::vector<ExprValue> result;
    cb(ResolveStaticArray(array, array_type, begin_index, end_index));
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

ErrOrValueVector ResolveArray(const fxl::RefPtr<EvalContext>& eval_context, const ExprValue& array,
                              size_t begin_index, size_t end_index) {
  if (!array.type())
    return Err("No type information.");

  fxl::RefPtr<Type> concrete = eval_context->GetConcreteType(array.type());
  if (const ArrayType* array_type = concrete->AsArrayType())
    return ResolveStaticArray(array, array_type, begin_index, end_index);
  return Err("Can't dereference a non-array type.");
}

void ResolveArray(const fxl::RefPtr<EvalContext>& eval_context, const ExprValue& array,
                  size_t begin_index, size_t end_index, fit::callback<void(ErrOrValueVector)> cb) {
  if (!DoResolveArray(eval_context, array, begin_index, end_index, std::move(cb)))
    cb(Err("Can't dereference a non-pointer or array type."));
}

void ResolveArrayItem(const fxl::RefPtr<EvalContext>& eval_context, const ExprValue& array,
                      size_t index, EvalCallback cb) {
  // This callback might possibly be bound to the regular array access function and we won't know
  // if it was needed until the function returns. We want to try regular resolution first to avoid
  // over-triggering pretty-printing if something is configured incorrectly. This case is not
  // performance sensitive so this extra allocation doesn't matter much.
  auto shared_cb = std::make_shared<EvalCallback>(std::move(cb));

  // Try a regular access first.
  if (DoResolveArray(eval_context, array, index, index + 1, [shared_cb](ErrOrValueVector result) {
        if (result.has_error())
          (*shared_cb)(result.err());
        else if (result.value().empty())  // Short read.
          (*shared_cb)(Err("Invalid array index."));
        else
          (*shared_cb)(std::move(result.value()[0]));  // Should have only one value.
      }))
    return;  // Handled by the regular array access.

  // Check for pretty types that support array access, shared_cb is our responsibility.
  if (const PrettyType* pretty = eval_context->GetPrettyTypeManager().GetForType(array.type())) {
    if (auto array_access = pretty->GetArrayAccess())
      return array_access(eval_context, array, index, std::move(*shared_cb));
  }

  (*shared_cb)(Err("Can't resolve an array access on type '%s'.",
                   array.type() ? array.type()->GetFullName().c_str() : "<Unknown>"));
}

}  // namespace zxdb
