// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/expr/return_value.h"

#include "src/developer/debug/zxdb/expr/abi.h"
#include "src/developer/debug/zxdb/expr/cast.h"
#include "src/developer/debug/zxdb/expr/resolve_ptr_ref.h"
#include "src/developer/debug/zxdb/symbols/base_type.h"
#include "src/developer/debug/zxdb/symbols/collection.h"
#include "src/developer/debug/zxdb/symbols/function.h"
#include "src/developer/debug/zxdb/symbols/modified_type.h"
#include "src/developer/debug/zxdb/symbols/symbol_data_provider.h"

namespace zxdb {

namespace {

Err GetUnsupportedReturnErr() {
  return Err(ErrType::kUnsupported, "The debugger doesn't support decoding this return type.");
}

void GetCollectionReturnValue(const fxl::RefPtr<EvalContext>& context, const Collection* coll,
                              fxl::RefPtr<Type> return_type, EvalCallback cb) {
  std::optional<Abi::CollectionReturn> ret_or =
      context->GetAbi()->GetCollectionReturnLocation(coll);
  if (!ret_or)
    return cb(GetUnsupportedReturnErr());

  // If we get here, the returned collection is indicated by an address indicated by the given
  // register upon function return.
  return context->GetDataProvider()->GetRegisterAsync(
      ret_or->addr_return_reg, [context, return_type, cb = std::move(cb)](
                                   const Err& err, std::vector<uint8_t> value) mutable {
        if (err.has_error())
          return cb(err);

        // Convert the register data to a pointer.
        TargetPointer address = 0;
        if (value.size() > 0)
          memcpy(&address, &value[0], sizeof(TargetPointer));

        // Convert the pointer to the actual collection.
        ResolvePointer(context, address, return_type, std::move(cb));
      });
}

}  // namespace

// This currently supports two types of return values: (1) numbers returned in registers, and (2)
// structures placed on the stack.
//
// Many smaller structures containing a single value or two integers can be returned in registers on
// many platforms, but decoding how these are laid out on each platform is more complicated and is
// left for a fun project for a future developer.
//
// The next enhancement would be to support structures returned by value containing only one
// BaseType member (say a smart pointer) which we can assume will normally be returned the same as
// the value itself (although this logic should be put in the Abi class since it may not apply to
// all platforms).
void GetReturnValue(const fxl::RefPtr<EvalContext>& context, const Function* func,
                    EvalCallback cb) {
  // Empty means void.
  if (!func->return_type())
    return cb(ExprValue());
  const Type* abstract_return_type = func->return_type().Get()->AsType();
  if (!abstract_return_type)
    return cb(ExprValue());
  fxl::RefPtr<Type> return_type = context->GetConcreteType(abstract_return_type);
  if (!return_type)
    return cb(ExprValue());

  std::optional<Abi::RegisterReturn> reg_or;

  if (const BaseType* base_type = return_type->AsBaseType()) {
    if (base_type->base_type() == BaseType::kBaseTypeNone) {
      // This means void (we want to differentiate this from failure to find the register).
      return cb(ExprValue());
    }
    reg_or = context->GetAbi()->GetReturnRegisterForBaseType(base_type);
  } else if (const ModifiedType* mod_type = return_type->AsModifiedType()) {
    // Modified types.
    if (mod_type->tag() == DwarfTag::kPointerType || mod_type->tag() == DwarfTag::kReferenceType ||
        mod_type->tag() == DwarfTag::kRvalueReferenceType) {
      // Pointers and references just act like integers.
      reg_or = Abi::RegisterReturn{.reg = context->GetAbi()->GetReturnRegisterForMachineInt(),
                                   .base_type = Abi::RegisterReturn::kInt};
    }
  } else if (const Enumeration* enum_type = return_type->AsEnumeration()) {
    // All enums should fit into machine words.
    reg_or = Abi::RegisterReturn{.reg = context->GetAbi()->GetReturnRegisterForMachineInt(),
                                 .base_type = Abi::RegisterReturn::kInt};
  } else if (const Collection* coll_type = return_type->AsCollection()) {
    // Collections are complex and handled separately.
    return GetCollectionReturnValue(context, coll_type, return_type, std::move(cb));
  }

  if (!reg_or) {
    // Complex return type that doesn't fit into a single register.
    return cb(GetUnsupportedReturnErr());
  }

  // If we get here the result is in a register.
  return context->GetDataProvider()->GetRegisterAsync(
      reg_or->reg, [context, reg_or = *reg_or, return_type, cb = std::move(cb)](
                       const Err& err, std::vector<uint8_t> value) mutable {
        if (err.has_error())
          return cb(err);
        if (value.size() < return_type->byte_size())
          return cb(Err("Return register unavailable."));

        // Construct a synthetic type to describe the register so we can cast from it.
        int register_base_type = BaseType::kBaseTypeNone;
        switch (reg_or.base_type) {
          case Abi::RegisterReturn::kFloat:
            register_base_type = BaseType::kBaseTypeFloat;
            break;
          case Abi::RegisterReturn::kInt:
            register_base_type = BaseType::kBaseTypeUnsigned;
            break;
        }
        auto reg_type =
            fxl::MakeRefCounted<BaseType>(register_base_type, value.size(), "RegisterValue");

        // Do the cast to the final type. This will end up truncating integers that are smaller
        // than a register, but doing a more complex conversion for floats.
        ExprValue reg_value(reg_type, std::move(value));
        auto result_or =
            CastNumericExprValue(context, reg_value, return_type, ExprValueSource(reg_or.reg));
        cb(std::move(result_or));
      });
}

}  // namespace zxdb
