// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/expr/resolve_collection.h"

#include "garnet/bin/zxdb/expr/expr_eval_context.h"
#include "garnet/bin/zxdb/expr/expr_value.h"
#include "garnet/bin/zxdb/expr/resolve_ptr_ref.h"
#include "garnet/bin/zxdb/symbols/arch.h"
#include "garnet/bin/zxdb/symbols/collection.h"
#include "garnet/bin/zxdb/symbols/data_member.h"
#include "garnet/bin/zxdb/symbols/function.h"
#include "garnet/bin/zxdb/symbols/inherited_from.h"
#include "garnet/bin/zxdb/symbols/modified_type.h"
#include "garnet/bin/zxdb/symbols/symbol_data_provider.h"
#include "garnet/bin/zxdb/symbols/type_utils.h"
#include "garnet/bin/zxdb/symbols/variable.h"
#include "garnet/bin/zxdb/symbols/visit_scopes.h"
#include "lib/fxl/strings/string_printf.h"

namespace zxdb {

namespace {

// Tries to interpret the type as a pointed to a Collection. On success,
// places the output into |*coll|.
Err GetPointedToCollection(const Type* type, const Collection** coll) {
  const Type* pointed_to = nullptr;
  Err err = GetPointedToType(type, &pointed_to);
  if (err.has_error())
    return err;

  *coll = pointed_to->GetConcreteType()->AsCollection();
  if (!coll) {
    return Err(
        "Attempting to dereference a pointer to '%s' which "
        "is not a class or a struct.",
        pointed_to->GetFullName().c_str());
  }
  return Err();
}

// A wrapper around FindMember that issues errors rather than returning
// an optional. The base can be null for the convenience of the caller. On
// error, the output FoundMember will be untouched.
Err FindMemberWithErr(const Collection* base, const std::string& member_name,
                      FoundMember* out) {
  if (!base) {
    return Err("Can't resolve '%s' on non-struct/class/union value.",
               member_name.c_str());
  }

  if (auto found = FindMember(base, member_name)) {
    *out = *found;
    return Err();
  }

  return Err("No member '%s' in %s '%s'.", member_name.c_str(),
             base->GetKindString(), base->GetFullName().c_str());
}

Err GetErrorForInvalidMemberOf(const Collection* coll) {
  return Err("Invalid data member for %s '%s'.", coll->GetKindString(),
             coll->GetFullName().c_str());
}

// Tries to describe the type of the value as best as possible when a member
// access is invalid.
Err GetErrorForInvalidMemberOf(const ExprValue& value) {
  if (!value.type())
    return Err("No type information.");

  if (const Collection* coll = value.type()->AsCollection())
    return GetErrorForInvalidMemberOf(coll);

  // Something other than a collection is the base.
  return Err("Accessing a member of non-struct/class/union '%s'.",
             value.type()->GetFullName().c_str());
}

// Validates the input member (it will null check) and extracts the type
// for the member.
Err GetMemberType(const Collection* coll, const DataMember* member,
                  fxl::RefPtr<Type>* member_type) {
  if (!member)
    return GetErrorForInvalidMemberOf(coll);

  *member_type =
      fxl::RefPtr<Type>(const_cast<Type*>(member->type().Get()->AsType()));
  if (!*member_type) {
    return Err("Bad type information for '%s.%s'.", coll->GetFullName().c_str(),
               member->GetAssignedName().c_str());
  }
  return Err();
}

void DoResolveMemberByPointer(fxl::RefPtr<ExprEvalContext> context,
                              const ExprValue& base_ptr,
                              const Collection* pointed_to_type,
                              const FoundMember& member,
                              std::function<void(const Err&, ExprValue)> cb) {
  Err err = base_ptr.EnsureSizeIs(kTargetPointerSize);
  if (err.has_error()) {
    cb(err, ExprValue());
    return;
  }

  fxl::RefPtr<Type> member_type;
  err = GetMemberType(pointed_to_type, member.data_member(), &member_type);
  if (err.has_error()) {
    cb(err, ExprValue());
    return;
  }

  TargetPointer base_address = base_ptr.GetAs<TargetPointer>();
  ResolvePointer(context->GetDataProvider(),
                 base_address + member.data_member_offset(),
                 std::move(member_type), std::move(cb));
}

// Extracts an embedded type inside of a base. This can be used for finding
// collection data members and inherited classes, both of which consist of a
// type and an offset.
Err ExtractSubType(const ExprValue& base, fxl::RefPtr<Type> sub_type,
                   uint32_t offset, ExprValue* out) {
  uint32_t size = sub_type->byte_size();
  if (offset + size > base.data().size())
    return GetErrorForInvalidMemberOf(base);
  std::vector<uint8_t> member_data(base.data().begin() + offset,
                                   base.data().begin() + (offset + size));

  *out = ExprValue(std::move(sub_type), std::move(member_data),
                   base.source().GetOffsetInto(offset));
  return Err();
}

// This variant takes a precomputed offset of the data member in the base
// class. This is to support the case where the data member is in a derived
// class (the derived class will have its own offset).
Err DoResolveMember(const ExprValue& base, const FoundMember& member,
                    ExprValue* out) {
  const Collection* coll = nullptr;
  if (!base.type() || !(coll = base.type()->GetConcreteType()->AsCollection()))
    return Err("Can't resolve data member on non-struct/class value.");

  fxl::RefPtr<Type> member_type;
  Err err = GetMemberType(coll, member.data_member(), &member_type);
  if (err.has_error())
    return err;

  return ExtractSubType(base, std::move(member_type),
                        member.data_member_offset(), out);
}

}  // namespace

std::optional<FoundMember> FindMember(const Collection* object,
                                      const std::string& member_name) {
  // This code will check the object and all base classes.
  std::optional<FoundMember> result;
  VisitClassHierarchy(
      object,
      [&member_name, &result](const Collection* cur_collection,
                              uint32_t cur_offset) -> bool {
        // Called for each collection in the hierarchy.
        for (const auto& lazy : cur_collection->data_members()) {
          const DataMember* data = lazy.Get()->AsDataMember();
          if (data && data->GetAssignedName() == member_name) {
            result.emplace(data, cur_offset + data->member_location());
            return true;
          }
        }
        return false;  // Not found in this scope, continue search.
      });
  return result;
}

std::optional<FoundVariable> FindMemberOnThis(const CodeBlock* block,
                                              const std::string& member_name) {
  // Find the function to see if it has a |this| pointer.
  const Function* function = block->GetContainingFunction();
  if (!function || !function->object_pointer())
    return std::nullopt;  // No "this" pointer.

  // The "this" variable.
  const Variable* this_var = function->object_pointer().Get()->AsVariable();
  if (!this_var)
    return std::nullopt;  // Symbols likely corrupt.

  // Pointed-to type for "this".
  const Collection* collection = nullptr;
  if (GetPointedToCollection(this_var->type().Get()->AsType(), &collection)
          .has_error())
    return std::nullopt;  // Symbols likely corrupt.

  if (auto member = FindMember(collection, member_name))
    return FoundVariable(this_var, std::move(*member));
  return std::nullopt;
}

Err ResolveMember(const ExprValue& base, const DataMember* member,
                  ExprValue* out) {
  if (!member)
    return GetErrorForInvalidMemberOf(base);
  return DoResolveMember(base, FoundMember(member, member->member_location()),
                         out);
}

Err ResolveMember(const ExprValue& base, const std::string& member_name,
                  ExprValue* out) {
  if (!base.type())
    return Err("No type information.");

  FoundMember found;
  Err err = FindMemberWithErr(base.type()->GetConcreteType()->AsCollection(),
                              member_name, &found);
  if (err.has_error())
    return err;
  return DoResolveMember(base, found, out);
}

void ResolveMemberByPointer(fxl::RefPtr<ExprEvalContext> context,
                            const ExprValue& base_ptr,
                            const FoundMember& found_member,
                            std::function<void(const Err&, ExprValue)> cb) {
  const Collection* coll = nullptr;
  Err err = GetPointedToCollection(base_ptr.type(), &coll);
  if (err.has_error()) {
    cb(err, ExprValue());
    return;
  }

  DoResolveMemberByPointer(context, base_ptr, coll, found_member,
                           std::move(cb));
}

void ResolveMemberByPointer(
    fxl::RefPtr<ExprEvalContext> context, const ExprValue& base_ptr,
    const std::string& member_name,
    std::function<void(const Err&, fxl::RefPtr<DataMember>, ExprValue)> cb) {
  const Collection* coll = nullptr;
  Err err = GetPointedToCollection(base_ptr.type(), &coll);
  if (err.has_error()) {
    cb(err, nullptr, ExprValue());
    return;
  }

  FoundMember found_member;
  err = FindMemberWithErr(coll, member_name, &found_member);
  if (err.has_error()) {
    cb(err, nullptr, ExprValue());
    return;
  }

  DoResolveMemberByPointer(
      context, base_ptr, coll, found_member,
      [cb = std::move(cb),
       member_ref = fxl::RefPtr<DataMember>(const_cast<DataMember*>(
           found_member.data_member()))](const Err& err, ExprValue value) {
        cb(err, std::move(member_ref), std::move(value));
      });
}

Err ResolveInherited(const ExprValue& value, const InheritedFrom* from,
                     ExprValue* out) {
  const Type* from_type = from->from().Get()->AsType();
  if (!from_type)
    return GetErrorForInvalidMemberOf(value);

  return ExtractSubType(value, fxl::RefPtr<Type>(const_cast<Type*>(from_type)),
                        from->offset(), out);
}

}  // namespace zxdb
