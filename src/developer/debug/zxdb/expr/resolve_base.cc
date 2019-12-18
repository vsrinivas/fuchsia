// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/expr/resolve_base.h"

#include "src/developer/debug/zxdb/common/string_util.h"
#include "src/developer/debug/zxdb/expr/cast.h"
#include "src/developer/debug/zxdb/expr/eval_context.h"
#include "src/developer/debug/zxdb/expr/expr_parser.h"
#include "src/developer/debug/zxdb/expr/find_name.h"
#include "src/developer/debug/zxdb/expr/resolve_collection.h"
#include "src/developer/debug/zxdb/expr/resolve_ptr_ref.h"
#include "src/developer/debug/zxdb/symbols/collection.h"
#include "src/developer/debug/zxdb/symbols/data_member.h"
#include "src/developer/debug/zxdb/symbols/elf_symbol.h"
#include "src/developer/debug/zxdb/symbols/location.h"
#include "src/developer/debug/zxdb/symbols/modified_type.h"
#include "src/developer/debug/zxdb/symbols/symbol_utils.h"

namespace zxdb {

namespace {

// When a class has a vtable, the pointer to the vtable is generated as a member of the class' data.
// This member is marked with DW_AT_artificial and named "_vptr.MyClass" by GCC and "_vptr$MyClass"
// by Clang, where "MyClass" is the name of the class. There is no scoping information on the name
// (namespaces, etc.).
const char kVtableMemberPrefix[] = "_vptr";

// The Clang demangler produces this prefix for vtable symbols.
const char kVtableSymbolNamePrefix[] = "vtable for ";

}  // namespace

void PromotePtrRefToDerived(const fxl::RefPtr<EvalContext>& context, ExprValue value,
                            EvalCallback cb) {
  if (!value.type())
    return cb(std::move(value));

  // Type must be a pointer or a reference.
  //
  // The code would be a little simpler if we just tried to dereference the pointer/reference and
  // then check for the vtable member. But this will be called a lot when evaluating collections,
  // usually won't match, and the dereference will require a slow memory fetch. By checking the
  // pointed-to/referenced type first, we avoid this overhead.
  fxl::RefPtr<Type> input_concrete = context->GetConcreteType(value.type());
  const ModifiedType* mod_type = input_concrete->AsModifiedType();
  if (!mod_type)
    return cb(std::move(value));
  if (!DwarfTagIsPointerOrReference(mod_type->tag()))
    return cb(std::move(value));

  // Referenced type must be a collection.
  const Type* type = mod_type->modified().Get()->AsType();
  if (!type)
    return cb(std::move(value));
  fxl::RefPtr<Type> modified_concrete = context->GetConcreteType(type);
  if (!modified_concrete)
    return cb(std::move(value));
  const Collection* modified_collection = modified_concrete->AsCollection();
  if (!modified_collection)
    return cb(std::move(value));

  // Referenced collection must have a vtable pointer.
  fxl::RefPtr<DataMember> vtable_member = GetVtableMember(modified_collection);
  if (!vtable_member)
    return cb(std::move(value));

  // Type is a pointer or reference to a virtual type. Get the vtable pointer value to see where it
  // goes.
  TargetPointer object_loc = 0;
  if (value.PromoteTo64(&object_loc).has_error())
    return cb(std::move(value));

  // Get the value of the vtable member.
  TargetPointer vtable_member_loc = object_loc + vtable_member->member_location();
  ResolvePointer(
      context, vtable_member_loc, RefPtrTo(vtable_member->type().Get()->AsType()),
      [context, original_value = std::move(value), modifier_tag = mod_type->tag(),
       modified_type = RefPtrTo(type), cb = std::move(cb)](ErrOrValue result) mutable {
        if (result.has_error())
          return cb(std::move(original_value));

        TargetPointer vtable = 0;
        if (result.value().PromoteTo64(&vtable).has_error())
          return cb(std::move(original_value));

        fxl::RefPtr<Type> derived_type = DerivedTypeForVtable(context, vtable);
        if (!derived_type)
          return cb(std::move(original_value));

        // Cast to the desired destination type. It should have the same type pattern as the
        // original: [ <C-V qualifier> ] + <pointer or reference> + [ <C-V qualifier> ] We did two
        // GetConcreteType() calls on each side of the ptr/ref and those stripped qualifiers need to
        // be put back.
        //
        // This code isn't perfect and will get confused if there are typedefs. Copying the C-V
        // qualifier will stop at typedefs, but the typedef could expand to something with a
        // qualifier like "const Foo" and this code would miss it. This gets very complicated and
        // the debugger doesn't actually follow qualifiers. This seems good enough for now.
        auto dest_type = AddCVQualifiersToMatch(modified_type.get(), std::move(derived_type));
        dest_type = fxl::MakeRefCounted<ModifiedType>(modifier_tag, std::move(dest_type));
        dest_type = AddCVQualifiersToMatch(original_value.type(), std::move(dest_type));
        CastExprValue(context, CastType::kStatic, original_value, dest_type, ExprValueSource(),
                      std::move(cb));
      });
}

fxl::RefPtr<DataMember> GetVtableMember(const Collection* coll) {
  for (const auto& lazy_member : coll->data_members()) {
    const DataMember* member = lazy_member.Get()->AsDataMember();
    if (!member)
      continue;

    if (member->artificial() && StringBeginsWith(member->GetAssignedName(), kVtableMemberPrefix))
      return RefPtrTo(member);
  }
  return fxl::RefPtr<DataMember>();
}

std::string TypeNameForVtableSymbolName(const std::string& sym_name) {
  if (!StringBeginsWith(sym_name, kVtableSymbolNamePrefix))
    return std::string();
  return sym_name.substr(std::size(kVtableSymbolNamePrefix) - 1);  // Trim the prefix w/o the null.
}

fxl::RefPtr<Type> DerivedTypeForVtable(const fxl::RefPtr<EvalContext>& context, TargetPointer ptr) {
  Location loc = context->GetLocationForAddress(ptr);
  if (!loc.symbol())
    return nullptr;

  // Expect vtable symbols to be ELF ones. There won't be DWARF entries since they don't appear in
  // the program.
  const ElfSymbol* elf_symbol = loc.symbol().Get()->AsElfSymbol();
  if (!elf_symbol)
    return nullptr;

  std::string type_name = TypeNameForVtableSymbolName(elf_symbol->GetAssignedName());
  if (type_name.empty())
    return nullptr;  // Not a vtable entry.

  ParsedIdentifier ident;
  if (ExprParser::ParseIdentifier(type_name, &ident).has_error())
    return nullptr;  // Type name not parseable.

  return context->ResolveForwardDefinition(std::move(ident));
}

}  // namespace zxdb
