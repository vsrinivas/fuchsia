// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/expr/resolve_collection.h"

#include "src/developer/debug/shared/message_loop.h"
#include "src/developer/debug/zxdb/expr/async_dwarf_expr_eval.h"
#include "src/developer/debug/zxdb/expr/bitfield.h"
#include "src/developer/debug/zxdb/expr/eval_context.h"
#include "src/developer/debug/zxdb/expr/expr_parser.h"
#include "src/developer/debug/zxdb/expr/expr_value.h"
#include "src/developer/debug/zxdb/expr/find_name.h"
#include "src/developer/debug/zxdb/expr/resolve_base.h"
#include "src/developer/debug/zxdb/expr/resolve_const_value.h"
#include "src/developer/debug/zxdb/expr/resolve_ptr_ref.h"
#include "src/developer/debug/zxdb/symbols/arch.h"
#include "src/developer/debug/zxdb/symbols/base_type.h"
#include "src/developer/debug/zxdb/symbols/collection.h"
#include "src/developer/debug/zxdb/symbols/data_member.h"
#include "src/developer/debug/zxdb/symbols/dwarf_expr_eval.h"
#include "src/developer/debug/zxdb/symbols/function.h"
#include "src/developer/debug/zxdb/symbols/identifier.h"
#include "src/developer/debug/zxdb/symbols/inherited_from.h"
#include "src/developer/debug/zxdb/symbols/modified_type.h"
#include "src/developer/debug/zxdb/symbols/symbol_data_provider.h"
#include "src/developer/debug/zxdb/symbols/variable.h"
#include "src/lib/fxl/strings/string_printf.h"

namespace zxdb {

namespace {

// A wrapper around FindMember that issues errors rather than returning an optional. The base can be
// null for the convenience of the caller. On error, the output FoundMember will be untouched.
ErrOr<FoundMember> FindMemberWithErr(const Collection* base, const ParsedIdentifier& identifier) {
  if (!base) {
    return Err("Can't resolve '%s' on non-struct/class/union value.",
               identifier.GetFullName().c_str());
  }

  FindNameOptions options(FindNameOptions::kNoKinds);
  options.find_vars = true;

  std::vector<FoundName> found;
  FindMember(FindNameContext(), options, base, identifier, nullptr, &found);
  if (!found.empty()) {
    FXL_DCHECK(found[0].kind() == FoundName::kMemberVariable);
    return found[0].member();
  }

  return Err("No member '%s' in %s '%s'.", identifier.GetFullName().c_str(), base->GetKindString(),
             base->GetFullName().c_str());
}

// Variant of the above that extracts the collection type from the given base value.
ErrOr<FoundMember> FindMemberWithErr(const fxl::RefPtr<EvalContext>& context, const ExprValue& base,
                                     const ParsedIdentifier& identifier) {
  fxl::RefPtr<Type> concrete_base = base.GetConcreteType(context.get());
  if (!concrete_base)
    return Err("No type information for collection.");
  return FindMemberWithErr(concrete_base->AsCollection(), identifier);
}

Err GetErrorForInvalidMemberOf(const Collection* coll) {
  return Err("Invalid data member for %s '%s'.", coll->GetKindString(),
             coll->GetFullName().c_str());
}

// Tries to describe the type of the value as best as possible when a member access is invalid.
Err GetErrorForInvalidMemberOf(const ExprValue& value) {
  if (!value.type())
    return Err("No type information.");

  if (const Collection* coll = value.type()->AsCollection())
    return GetErrorForInvalidMemberOf(coll);

  // Something other than a collection is the base.
  return Err("Accessing a member of non-struct/class/union '%s'.",
             value.type()->GetFullName().c_str());
}

// Validates the input member (it will null check) and extracts the type and size for the member.
// The size is returned separately because the member_type might be a forward declaration and
// we can't return a concrete type without breaking CV qualifiers.
Err GetMemberType(const fxl::RefPtr<EvalContext>& context, const Collection* coll,
                  const DataMember* member, fxl::RefPtr<Type>* member_type, uint32_t* member_size) {
  if (!member)
    return GetErrorForInvalidMemberOf(coll);

  *member_type = RefPtrTo(member->type().Get()->AsType());
  if (!*member_type) {
    return Err("Bad type information for '%s.%s'.", coll->GetFullName().c_str(),
               member->GetAssignedName().c_str());
  }

  return Err();
}

// Backend for ResolveMemberByPointer variants that does the actual memory fetch. It's given a
// concrete pointer and pointed-to type, along with a specific found member inside it.
void DoResolveMemberByPointer(const fxl::RefPtr<EvalContext>& context, const ExprValue& base_ptr,
                              const Collection* pointed_to_type, const FoundMember& member,
                              EvalCallback cb) {
  if (Err err = base_ptr.EnsureSizeIs(kTargetPointerSize); err.has_error())
    return cb(err);

  if (member.data_member()->is_bitfield()) {
    // The bitfield case is complicated. Get the full pointed-to collection value and then resolve
    // the member access using "." mode to re-use the non-pointer codepath. This avoids duplicating
    // the bitfield logic. (This is actually valid logic for every case but fetches unnecessary
    // memory which we avoid in the common case below).
    ResolvePointer(context, base_ptr,
                   [context, member, cb = std::move(cb)](ErrOrValue value) mutable {
                     if (value.has_error())
                       return cb(std::move(value));
                     cb(ResolveBitfieldMember(context, value.value(), member));
                   });
  } else {
    // Common case for non-bitfield members. We can avoid fetching the entire structure (which can
    // be very large in some edge cases) and just get fetch the memory for the item we need.
    fxl::RefPtr<Type> member_type;
    uint32_t member_size = 0;
    Err err =
        GetMemberType(context, pointed_to_type, member.data_member(), &member_type, &member_size);
    if (err.has_error())
      return cb(err);

    TargetPointer base_address = base_ptr.GetAs<TargetPointer>();
    // TODO(bug 41503) handle virtual inheritance.
    if (auto opt_offset = member.GetDataMemberOffset()) {
      ResolvePointer(context, base_address + *opt_offset, std::move(member_type), std::move(cb));
    } else {
      return cb(Err("Virtual inheritance is not supported yet (bug 41503)."));
    }
  }
}

// Implementation of ResolveMemberByPointer with a named member. This does everything except handle
// conversion to base classes.
void DoResolveMemberNameByPointer(const fxl::RefPtr<EvalContext>& context,
                                  const ExprValue& base_ptr, const ParsedIdentifier& identifier,
                                  fit::callback<void(ErrOrValue, const FoundMember&)> cb) {
  fxl::RefPtr<Collection> coll;
  if (Err err = GetConcretePointedToCollection(context, base_ptr.type(), &coll); err.has_error())
    return cb(err, FoundMember());

  ErrOr<FoundMember> found = FindMemberWithErr(coll.get(), identifier);
  if (found.has_error())
    return cb(found.err(), FoundMember());

  // Dispatch to low-level version now that the member is found by name.
  DoResolveMemberByPointer(context, base_ptr, coll.get(), found.value(),
                           [cb = std::move(cb), found = found.value()](ErrOrValue value) mutable {
                             cb(std::move(value), found);
                           });
}

// Extracts an embedded type inside of a base. This can be used for finding collection data members
// and inherited classes, both of which consist of a type and an offset.
ErrOrValue ExtractSubType(const fxl::RefPtr<EvalContext>& context, const ExprValue& base,
                          fxl::RefPtr<Type> sub_type, uint32_t offset) {
  // Need a valid size for the inside type so it has to be concrete.
  auto concrete = context->GetConcreteType(sub_type.get());
  uint32_t size = concrete->byte_size();

  if (offset + size > base.data().size()) {
    return Err("Invalid data offset %" PRIu32 " in object of size %zu.", offset,
               base.data().size());
  }
  std::vector<uint8_t> member_data(base.data().begin() + offset,
                                   base.data().begin() + (offset + size));

  return ExprValue(std::move(sub_type), std::move(member_data),
                   base.source().GetOffsetInto(offset));
}

// This variant takes a precomputed offset of the data member in the base class. This is to support
// the case where the data member is in a derived class (the derived class will have its own
// offset).
ErrOrValue DoResolveNonstaticMember(const fxl::RefPtr<EvalContext>& context, const ExprValue& base,
                                    const FoundMember& member) {
  // Bitfields get special handling.
  if (member.data_member()->is_bitfield())
    return ResolveBitfieldMember(context, base, member);

  // Constant value members.
  if (member.data_member()->const_value().has_value())
    return ResolveConstValue(context, member.data_member());

  fxl::RefPtr<Type> concrete_type = base.GetConcreteType(context.get());
  const Collection* coll = nullptr;
  if (!base.type() || !(coll = concrete_type->AsCollection()))
    return Err("Can't resolve data member on non-struct/class value.");

  fxl::RefPtr<Type> member_type;
  uint32_t member_size = 0;
  Err err = GetMemberType(context, coll, member.data_member(), &member_type, &member_size);
  if (err.has_error())
    return err;

  // TODO(bug 41503) handle virtual inheritance.
  if (auto opt_offset = member.GetDataMemberOffset())
    return ExtractSubType(context, base, std::move(member_type), *opt_offset);
  return Err("Virtual inheritance is not supported yet (bug 41503).");
}

// As with DoResolveNonstaticMember, this takes a precomputed offset. It is asynchronous to handle
// static data members that may require a memory fetch.
void DoResolveMember(const fxl::RefPtr<EvalContext>& context, const ExprValue& base,
                     const FoundMember& member, EvalCallback cb) {
  FXL_DCHECK(member.data_member());
  if (member.data_member()->is_external()) {
    // A forward-declared static member. In C++ static members can't be bitfields so we don't handle
    // them.
    return context->GetVariableValue(RefPtrTo(member.data_member()), std::move(cb));
  }

  // Normal nonstatic resolution is synchronous.
  cb(DoResolveNonstaticMember(context, base, member));
}

}  // namespace

void ResolveMember(const fxl::RefPtr<EvalContext>& context, const ExprValue& base,
                   const FoundMember& member, EvalCallback cb) {
  if (member.is_null())
    return cb(GetErrorForInvalidMemberOf(base));
  DoResolveMember(context, base, member, std::move(cb));
}

void ResolveMember(const fxl::RefPtr<EvalContext>& context, const ExprValue& base,
                   const ParsedIdentifier& identifier, EvalCallback cb) {
  ErrOr<FoundMember> found = FindMemberWithErr(context, base, identifier);
  if (found.has_error())
    return cb(found.err());
  DoResolveMember(context, base, found.value(), std::move(cb));
}

ErrOrValue ResolveNonstaticMember(const fxl::RefPtr<EvalContext>& context, const ExprValue& base,
                                  const FoundMember& member) {
  if (member.is_null())
    return GetErrorForInvalidMemberOf(base);
  return DoResolveNonstaticMember(context, base, member);
}

ErrOrValue ResolveNonstaticMember(const fxl::RefPtr<EvalContext>& context, const ExprValue& base,
                                  const ParsedIdentifier& identifier) {
  ErrOr<FoundMember> found = FindMemberWithErr(context, base, identifier);
  if (found.has_error())
    return found.err();
  return DoResolveNonstaticMember(context, base, found.value());
}

ErrOrValue ResolveNonstaticMember(const fxl::RefPtr<EvalContext>& context, const ExprValue& base,
                                  std::initializer_list<std::string> names) {
  ExprValue cur = base;
  for (const std::string& name : names) {
    ParsedIdentifier id;
    if (Err err = ExprParser::ParseIdentifier(name, &id); err.has_error())
      return err;

    ErrOrValue result = ResolveNonstaticMember(context, cur, id);
    if (result.has_error())
      return result.err();

    cur = std::move(result.take_value());
  }
  return cur;
}

void ResolveMemberByPointer(const fxl::RefPtr<EvalContext>& context, const ExprValue& base_ptr,
                            const FoundMember& found_member, EvalCallback cb) {
  fxl::RefPtr<Collection> pointed_to;
  Err err = GetConcretePointedToCollection(context, base_ptr.type(), &pointed_to);
  if (err.has_error())
    return cb(err);

  DoResolveMemberByPointer(context, base_ptr, pointed_to.get(), found_member, std::move(cb));
}

void ResolveMemberByPointer(const fxl::RefPtr<EvalContext>& context, const ExprValue& base_ptr,
                            const ParsedIdentifier& identifier,
                            fit::callback<void(ErrOrValue, const FoundMember&)> cb) {
  if (context->ShouldPromoteToDerived()) {
    // Check to see if this is a reference to a base class that we can convert to a derived class.
    PromotePtrRefToDerived(context, PromoteToDerived::kPtrOnly, std::move(base_ptr),
                           [context, identifier, cb = std::move(cb)](ErrOrValue result) mutable {
                             if (result.has_error()) {
                               cb(result, {});
                             } else {
                               DoResolveMemberNameByPointer(context, result.take_value(),
                                                            identifier, std::move(cb));
                             }
                           });
  } else {
    // No magic base-class resolution is required, just check the pointer.
    DoResolveMemberNameByPointer(context, base_ptr, identifier, std::move(cb));
  }
}

void ResolveInherited(const fxl::RefPtr<EvalContext>& context, const ExprValue& value,
                      const InheritancePath& path, fit::callback<void(ErrOrValue)> cb) {
  auto final_type = path.base_ref();

  // Most cases have a constant offset and we can do that right away.
  if (auto opt_offset = path.BaseOffsetInDerived())
    return cb(ExtractSubType(context, value, std::move(final_type), *opt_offset));

  // Everything else is a DWARF expression which needs to be evaluated based on the pointers to the
  // data. This requires that the class have a memory address.
  if (value.source().type() != ExprValueSource::Type::kMemory || value.source().is_bitfield())
    return cb(Err("Can't evaluate virtual inheritance on an object without a memory address."));
  TargetPointer object_ptr = value.source().address();

  ResolveInheritedPtr(
      context, object_ptr, path,
      [context, value, object_ptr, final_type = std::move(final_type),
       cb = std::move(cb)](ErrOr<TargetPointer> base_ptr) mutable {
        if (base_ptr.has_error())
          return cb(base_ptr.err());

        // The resolved data "should" be inside the original class since it's a base class so we
        // don't have to re-request the memory from the target. Extract that if possible.
        auto concrete = context->GetConcreteType(final_type.get());
        uint32_t size = concrete->byte_size();
        if (base_ptr.value() >= object_ptr &&
            (base_ptr.value() - object_ptr + size <= value.data().size())) {
          return cb(ExtractSubType(context, value, final_type, base_ptr.value() - object_ptr));
        }

        // The resulting pointer isn't inside the derived class. While the DWARF spec never
        // guarantees this is the case, in our languages it will have to be for objects to make
        // any sense. If we detect this, assume memory is corrupted rather than report a
        // likely-incorrect address.
        cb(Err("Virtual base class '%s' (size %u) resolved to an address 0x%" PRIx64
               " outside of the derived class '%s' at 0x%" PRIx64 " (size %zu).",
               final_type->GetFullName().c_str(), size, base_ptr.value(),
               value.type()->GetFullName().c_str(), object_ptr, value.data().size()));
      });
}

void ResolveInheritedPtr(const fxl::RefPtr<EvalContext>& context, TargetPointer derived,
                         const InheritancePath& path,
                         fit::function<void(ErrOr<TargetPointer>)> cb) {
  // In the common case there will be a constant offset. This will also handle the identity cases
  // (they will come up when this is called recursively) where there is no inheritance.
  if (auto opt_offset = path.BaseOffsetInDerived())
    return cb(derived + *opt_offset);

  // Non-constant path, likely due to virtual inheritance. A path could have many steps, some of
  // which are constant offsets, and some of which are expressions for virtual inheritance. For
  // simplicity we do each step separately in a recursive manner.

  // Since an inheritance path includes both the base and derived class, there should be more than
  // one entry for there to be any inheritance.
  FXL_DCHECK(path.path().size() > 1);
  InheritancePath remaining = path.SubPath(1);  // Path left over after computing the first offset.

  // The first step of the inheritance chain is index 1's "from".
  const InheritedFrom* first_from = path.path()[1].from.get();
  FXL_DCHECK(first_from);
  switch (first_from->kind()) {
    case InheritedFrom::kConstant: {
      ResolveInheritedPtr(context, derived + first_from->offset(), remaining, std::move(cb));
      return;
    }
    case InheritedFrom::kExpression: {
      // Run the expression (may be asynchronous). We don't use the AsyncDwarfExprEval because
      // that attempts to create an ExprValue from the result, which normally means dereferencing
      // the result of the expression as a pointer. We want the literal number resulting from
      // evaluating the expression.
      auto dwarf_eval = std::make_shared<DwarfExprEval>();
      dwarf_eval->Push(derived);
      dwarf_eval->Eval(
          context->GetDataProvider(), first_from->GetSymbolContext(context->GetProcessSymbols()),
          first_from->location_expression(),
          [dwarf_eval, context, remaining = std::move(remaining), cb = std::move(cb)](
              DwarfExprEval*, const Err& err) mutable {
            if (err.has_error()) {
              cb(err);
            } else {
              // Continue resolution on any remaining inheritance steps.
              ResolveInheritedPtr(context, static_cast<TargetPointer>(dwarf_eval->GetResult()),
                                  remaining, std::move(cb));
            }

            // Prevent the DwarfExprEval from getting deleted from its own stack.
            debug_ipc::MessageLoop::Current()->PostTask(FROM_HERE,
                                                        [dwarf_eval = std::move(dwarf_eval)]() {});
          });
      return;
    }
  }

  FXL_NOTREACHED();
  cb(Err("Internal error"));
}

ErrOrValue ResolveInherited(const fxl::RefPtr<EvalContext>& context, const ExprValue& value,
                            const InheritedFrom* from) {
  const Type* from_type = from->from().Get()->AsType();
  if (!from_type)
    return GetErrorForInvalidMemberOf(value);

  return ExtractSubType(context, value, RefPtrTo(from_type), from->offset());
}

ErrOrValue ResolveInherited(const fxl::RefPtr<EvalContext>& context, const ExprValue& value,
                            fxl::RefPtr<Type> base_type, uint64_t offset) {
  return ExtractSubType(context, value, std::move(base_type), offset);
}

Err GetConcretePointedToCollection(const fxl::RefPtr<EvalContext>& eval_context, const Type* input,
                                   fxl::RefPtr<Collection>* pointed_to) {
  fxl::RefPtr<Type> to_type;
  if (Err err = GetPointedToType(eval_context, input, &to_type); err.has_error())
    return err;
  to_type = eval_context->GetConcreteType(to_type.get());

  if (const Collection* collection = to_type->AsCollection()) {
    *pointed_to = fxl::RefPtr<Collection>(const_cast<Collection*>(collection));
    return Err();
  }

  return Err("Attempting to dereference a pointer to '%s' which is not a class, struct, or union.",
             to_type->GetFullName().c_str());
}

}  // namespace zxdb
