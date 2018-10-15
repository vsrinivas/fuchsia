// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/expr/resolve_member.h"

#include "garnet/bin/zxdb/expr/expr_eval_context.h"
#include "garnet/bin/zxdb/expr/expr_value.h"
#include "garnet/bin/zxdb/expr/resolve_ptr_ref.h"
#include "garnet/bin/zxdb/symbols/collection.h"
#include "garnet/bin/zxdb/symbols/data_member.h"
#include "garnet/bin/zxdb/symbols/modified_type.h"
#include "garnet/bin/zxdb/symbols/symbol_data_provider.h"
#include "garnet/bin/zxdb/symbols/type_utils.h"
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
        fxl::StringPrintf("Attempting to dereference a pointer to '%s' which "
                          "is not a class or a struct.",
                          pointed_to->GetFullName().c_str()));
  }
  return Err();
}

// This can accept a null base pointer so the caller doesn't need to check.
//
// On success, fills |*out| and |*offset|. |*offset| will be the offset from
// the beginning of |Collection| to the data member. For direct member
// accesses this will be the same as (*out)->member_location() but it will
// also take into account if the member is in a base class that itself has its
// own offset from the base.
Err FindMemberNamed(const Collection* base, const std::string& member_name,
                    const DataMember** out, uint32_t* offset) {
  if (!base) {
    return Err(
        fxl::StringPrintf("Can't resolve '%s' on non-struct/class/union value.",
                          member_name.c_str()));
  }

  // Check the class and all of its base classes.
  bool found = VisitClassHierarchy(
      base, [member_name, out, offset](const Collection* cur_collection,
                                       uint32_t cur_offset) -> bool {
        // Called for each collection in the hierarchy.
        for (const auto& lazy : cur_collection->data_members()) {
          const DataMember* data = lazy.Get()->AsDataMember();
          if (data && data->GetAssignedName() == member_name) {
            *out = data;
            *offset = cur_offset + data->member_location();
            return true;
          }
        }
        return false;  // Not found in this scope, continue search.
      });

  if (found)
    return Err();  // Out vars already filled in.
  return Err(fxl::StringPrintf("No member '%s' in %s '%s'.",
                               member_name.c_str(), base->GetKindString(),
                               base->GetFullName().c_str()));
}

// Validates the input member (it will null check) and extracts the type
// for the member.
Err GetMemberType(const Collection* coll, const DataMember* member,
                  fxl::RefPtr<Type>* member_type) {
  if (!member) {
    return Err(fxl::StringPrintf("Invalid data member for %s '%s'.",
                                 coll->GetKindString(),
                                 coll->GetFullName().c_str()));
  }

  *member_type =
      fxl::RefPtr<Type>(const_cast<Type*>(member->type().Get()->AsType()));
  if (!*member_type) {
    return Err(fxl::StringPrintf("Bad type information for '%s.%s'.",
                                 coll->GetFullName().c_str(),
                                 member->GetAssignedName().c_str()));
  }
  return Err();
}

void DoResolveMemberByPointer(fxl::RefPtr<ExprEvalContext> context,
                              const ExprValue& base_ptr,
                              const Collection* pointed_to_type,
                              const DataMember* member,
                              std::function<void(const Err&, ExprValue)> cb) {
  Err err = base_ptr.EnsureSizeIs(sizeof(uint64_t));
  if (err.has_error()) {
    cb(err, ExprValue());
    return;
  }

  fxl::RefPtr<Type> member_type;
  err = GetMemberType(pointed_to_type, member, &member_type);
  if (err.has_error()) {
    cb(err, ExprValue());
    return;
  }

  uint64_t base_address = base_ptr.GetAs<uint64_t>();
  uint32_t offset = member->member_location();
  ResolvePointer(context->GetDataProvider(), base_address + offset,
                 std::move(member_type), std::move(cb));
}

// This variant takes a precomputed ofsfet of the data member in the base
// class. This is to support the case where the data member is in a derived
// class and the offset is itself offset.
Err DoResolveMember(const ExprValue& base, const DataMember* member,
                    uint32_t offset, ExprValue* out) {
  const Collection* coll = nullptr;
  if (!base.type() || !(coll = base.type()->GetConcreteType()->AsCollection()))
    return Err("Can't resolve data member on non-struct/class value.");

  fxl::RefPtr<Type> member_type;
  Err err = GetMemberType(coll, member, &member_type);
  if (err.has_error())
    return err;

  // Extract the data.
  uint32_t size = member_type->byte_size();
  if (offset + size > base.data().size()) {
    return Err(fxl::StringPrintf(
        "Member value '%s' is outside of the data of base '%s'. Please file a "
        "bug with a repro.",
        member->GetAssignedName().c_str(), coll->GetFullName().c_str()));
  }
  std::vector<uint8_t> member_data(base.data().begin() + offset,
                                   base.data().begin() + (offset + size));

  *out = ExprValue(std::move(member_type), std::move(member_data),
                   base.source().GetOffsetInto(offset));
  return Err();
}

}  // namespace

Err ResolveMember(const ExprValue& base, const DataMember* member,
                  ExprValue* out) {
  const Collection* coll = nullptr;
  if (!base.type() || !(coll = base.type()->GetConcreteType()->AsCollection()))
    return Err("Can't resolve data member on non-struct/class value.");

  fxl::RefPtr<Type> member_type;
  Err err = GetMemberType(coll, member, &member_type);
  if (err.has_error())
    return err;

  // Extract the data.
  uint32_t offset = member->member_location();
  uint32_t size = member_type->byte_size();
  if (offset + size > base.data().size()) {
    return Err(fxl::StringPrintf(
        "Member value '%s' is outside of the data of base '%s'. Please file a "
        "bug with a repro.",
        member->GetAssignedName().c_str(), coll->GetFullName().c_str()));
  }
  std::vector<uint8_t> member_data(base.data().begin() + offset,
                                   base.data().begin() + (offset + size));

  *out = ExprValue(std::move(member_type), std::move(member_data),
                   base.source().GetOffsetInto(offset));
  return Err();
}

Err ResolveMember(const ExprValue& base, const std::string& member_name,
                  ExprValue* out) {
  if (!base.type())
    return Err("No type information.");

  const DataMember* member = nullptr;
  uint32_t member_offset = 0;
  Err err = FindMemberNamed(base.type()->GetConcreteType()->AsCollection(),
                            member_name, &member, &member_offset);
  if (err.has_error())
    return err;
  return DoResolveMember(base, member, member_offset, out);
}

void ResolveMemberByPointer(fxl::RefPtr<ExprEvalContext> context,
                            const ExprValue& base_ptr, const DataMember* member,
                            std::function<void(const Err&, ExprValue)> cb) {
  const Collection* coll = nullptr;
  Err err = GetPointedToCollection(base_ptr.type(), &coll);
  if (err.has_error()) {
    cb(err, ExprValue());
    return;
  }

  DoResolveMemberByPointer(context, base_ptr, coll, member, std::move(cb));
}

void ResolveMemberByPointer(fxl::RefPtr<ExprEvalContext> context,
                            const ExprValue& base_ptr,
                            const std::string& member_name,
                            std::function<void(const Err&, ExprValue)> cb) {
  const Collection* coll = nullptr;
  Err err = GetPointedToCollection(base_ptr.type(), &coll);
  if (err.has_error()) {
    cb(err, ExprValue());
    return;
  }

  const DataMember* member = nullptr;
  uint32_t member_offset = 0;
  err = FindMemberNamed(coll, member_name, &member, &member_offset);
  if (err.has_error()) {
    cb(err, ExprValue());
    return;
  }

  DoResolveMemberByPointer(context, base_ptr, coll, member, std::move(cb));
}

}  // namespace zxdb
