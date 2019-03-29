// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/symbols/type_test_support.h"

#include "src/developer/debug/zxdb/symbols/base_type.h"
#include "src/developer/debug/zxdb/symbols/collection.h"
#include "src/developer/debug/zxdb/symbols/data_member.h"
#include "src/developer/debug/zxdb/symbols/inherited_from.h"

namespace zxdb {

fxl::RefPtr<BaseType> MakeInt32Type() {
  return fxl::MakeRefCounted<BaseType>(BaseType::kBaseTypeSigned, 4, "int32_t");
}

fxl::RefPtr<BaseType> MakeUint32Type() {
  return fxl::MakeRefCounted<BaseType>(BaseType::kBaseTypeUnsigned, 4,
                                       "uint32_t");
}

fxl::RefPtr<BaseType> MakeInt64Type() {
  return fxl::MakeRefCounted<BaseType>(BaseType::kBaseTypeSigned, 8, "int64_t");
}

fxl::RefPtr<BaseType> MakeUint64Type() {
  return fxl::MakeRefCounted<BaseType>(BaseType::kBaseTypeUnsigned, 8,
                                       "uint64_t");
}

fxl::RefPtr<Collection> MakeCollectionType(
    DwarfTag type_tag, const std::string& type_name,
    std::initializer_list<NameAndType> members) {
  return MakeCollectionTypeWithOffset(type_tag, type_name, 0,
                                      std::move(members));
}

fxl::RefPtr<Collection> MakeCollectionTypeWithOffset(
    DwarfTag type_tag, const std::string& type_name,
    uint32_t first_member_offset, std::initializer_list<NameAndType> members) {
  auto result = fxl::MakeRefCounted<Collection>(type_tag);
  result->set_assigned_name(type_name);

  uint32_t offset = first_member_offset;
  std::vector<LazySymbol> data_members;
  for (const auto& [name, type] : members) {
    auto member = fxl::MakeRefCounted<DataMember>();
    member->set_assigned_name(name);
    member->set_type(LazySymbol(type));
    member->set_member_location(offset);
    data_members.emplace_back(member);

    offset += type->byte_size();
  }

  result->set_byte_size(offset);
  result->set_data_members(std::move(data_members));
  return result;
}

fxl::RefPtr<Collection> MakeDerivedClassPair(
    DwarfTag type_tag, const std::string& base_name,
    std::initializer_list<NameAndType> base_members,
    const std::string& derived_name,
    std::initializer_list<NameAndType> derived_members) {
  auto base = MakeCollectionTypeWithOffset(type_tag, base_name, 0,
                                           std::move(base_members));

  // Leave room at the beginning of |derived| for the base class.
  auto derived = MakeCollectionTypeWithOffset(
      type_tag, derived_name, base->byte_size(), std::move(derived_members));

  derived->set_inherited_from(
      {LazySymbol(fxl::MakeRefCounted<InheritedFrom>(LazySymbol(base), 0))});
  return derived;
}

}  // namespace zxdb
