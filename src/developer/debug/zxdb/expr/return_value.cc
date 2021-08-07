// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/expr/return_value.h"

#include <lib/syslog/cpp/macros.h>

#include "src/developer/debug/zxdb/expr/abi.h"
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

// Maps a register ID to its value.
using RegisterValueMap = std::map<debug::RegisterID, std::vector<uint8_t>>;

void GetCollectionReturnByRefValue(const fxl::RefPtr<EvalContext>& context, const Collection* coll,
                                   fxl::RefPtr<Type> return_type, EvalCallback cb) {
  std::optional<Abi::CollectionReturn> ret_or =
      context->GetAbi()->GetCollectionReturnByRefLocation(coll);
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
          memcpy(&address, value.data(), sizeof(TargetPointer));

        // Convert the pointer to the actual collection.
        ResolvePointer(context, address, return_type, std::move(cb));
      });
}

// Given the collection layout in registers and all of the corresponding register values, assembles
// the given collection as an ExprValue and returns it.
//
// The resulting value's type will be the given |return_type|. This does not need to be concrete.
ErrOrValue AssembleCollectionFromRegisters(fxl::RefPtr<Type> return_type,
                                           const Abi::CollectionByValueReturn& layout,
                                           const RegisterValueMap& values) {
  // Concatenate all the required data.
  std::vector<uint8_t> data;
  for (const auto& comp : layout.regs) {
    // Retrieve the register data.
    const auto& found = values.find(comp.reg);
    if (found == values.end())
      return Err("Register value not available.");
    const auto& register_bytes = found->second;
    if (register_bytes.size() < comp.bytes)
      return Err("Register value an unexpected size.");

    // Copy from the low bytes of the register value which we assume is little-endian.
    size_t cur_offset = data.size();
    data.resize(cur_offset + comp.bytes);
    memcpy(&data[cur_offset], register_bytes.data(), comp.bytes);
  }

  return ExprValue(std::move(return_type), std::move(data),
                   ExprValueSource(ExprValueSource::Type::kComposite));
}

void GetCollectionReturnByValueValue(const fxl::RefPtr<EvalContext>& context,
                                     const Collection* coll, fxl::RefPtr<Type> return_type,
                                     EvalCallback cb) {
  // Collect the registers used by the ABI to define this collection.
  std::optional<Abi::CollectionByValueReturn> ret_or =
      context->GetAbi()->GetCollectionReturnByValueLocation(context, coll);
  if (!ret_or)
    return cb(GetUnsupportedReturnErr());
  const Abi::CollectionByValueReturn& ret = *ret_or;
  FX_DCHECK(!ret.regs.empty());

  // Collect the required register values.
  std::vector<debug::RegisterID> required_regs;
  for (const auto& component : ret_or->regs)
    required_regs.push_back(component.reg);

  context->GetDataProvider()->GetRegisters(
      required_regs, [return_type = std::move(return_type), layout = ret_or.value(),
                      cb = std::move(cb)](const Err& err, RegisterValueMap map) mutable {
        if (err.has_error())
          return cb(err);
        cb(AssembleCollectionFromRegisters(std::move(return_type), layout, map));
      });
}

}  // namespace

void GetReturnValue(const fxl::RefPtr<EvalContext>& context, const Function* func,
                    EvalCallback cb) {
  // Empty means void.
  if (!func->return_type())
    return cb(ExprValue());

  // We want the type of the result to be the type declared by the function (including const,
  // typedefs, etc. making it abstract) but need to compute on the underlying concrete type, so
  // keep both values.
  fxl::RefPtr<Type> return_type = RefPtrTo(func->return_type().Get()->As<Type>());
  if (!return_type)
    return cb(Err("Invalid return type for function."));
  fxl::RefPtr<Type> concrete = context->GetConcreteType(return_type.get());

  // Handle collections (these are more complex so handled separately).
  if (const Collection* coll_type = concrete->As<Collection>()) {
    switch (coll_type->calling_convention()) {
      case Collection::kPassByReference:
        return GetCollectionReturnByRefValue(context, coll_type, return_type, std::move(cb));
      case Collection::kPassByValue:
        return GetCollectionReturnByValueValue(context, coll_type, return_type, std::move(cb));
      case Collection::kNormalCall:
        // Fall through to unsupported if the calling convention is not set. All our supported
        // compilers set this flag so it's not clear what the behavior should be if it's not set.
        //
        // It may mean that the debugger should figure out the calling convention by looking at the
        // structure, but since not all information is available about e.g. C++ copy constructors as
        // specified in the ABI, the debugger can't necessarily make the correct decision.
        break;
    }
    return cb(GetUnsupportedReturnErr());
  }

  // Everything else should be some normal value that goes in a register.
  std::optional<debug::RegisterID> reg_or;

  if (const BaseType* base_type = concrete->As<BaseType>()) {
    if (base_type->base_type() == BaseType::kBaseTypeNone) {
      // This means void (we want to differentiate this from failure to find the register).
      return cb(ExprValue());
    }
    reg_or = context->GetAbi()->GetReturnRegisterForBaseType(base_type);
  } else if (const ModifiedType* mod_type = concrete->As<ModifiedType>()) {
    // Modified types. This will be pointers and references, const and volatile will have been
    // removed by GetConcreteType() above.
    if (mod_type->tag() == DwarfTag::kPointerType || mod_type->tag() == DwarfTag::kReferenceType ||
        mod_type->tag() == DwarfTag::kRvalueReferenceType) {
      // Pointers and references just act like integers.
      reg_or = context->GetAbi()->GetReturnRegisterForMachineInt();
    }
  } else if (const Enumeration* enum_type = concrete->As<Enumeration>()) {
    // All enums should fit into machine words. If the register is too large, it will be truncated
    // below to only pick the low bytes.
    reg_or = context->GetAbi()->GetReturnRegisterForMachineInt();
  }

  if (!reg_or) {
    // Complex return type that doesn't fit into a single register.
    return cb(GetUnsupportedReturnErr());
  }

  // If we get here the result is a single value in a register.
  return context->GetDataProvider()->GetRegisterAsync(
      *reg_or, [context, reg = *reg_or, return_type, byte_size = concrete->byte_size(),
                cb = std::move(cb)](const Err& err, std::vector<uint8_t> value) mutable {
        if (err.has_error())
          return cb(err);
        if (value.size() < byte_size)
          return cb(Err("Return register unavailable."));

        // Assume little-endian so we can use the low bits when the register is larger than the
        // type.
        value.resize(byte_size);

        cb(ExprValue(std::move(return_type), std::move(value), ExprValueSource(reg)));
      });
}

}  // namespace zxdb
