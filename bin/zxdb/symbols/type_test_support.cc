// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/symbols/type_test_support.h"

#include "garnet/bin/zxdb/symbols/base_type.h"
#include "garnet/bin/zxdb/symbols/collection.h"
#include "garnet/bin/zxdb/symbols/data_member.h"

namespace zxdb {

fxl::RefPtr<BaseType> MakeInt32Type() {
  return fxl::MakeRefCounted<BaseType>(BaseType::kBaseTypeSigned, 4, "int32_t");
}

// Defines a structure with two members of the given name and type. The members
// will immediately follow each other in memory.
fxl::RefPtr<Collection> MakeStruct2Members(const std::string& struct_name,
                                           fxl::RefPtr<Type> member_1_type,
                                           const std::string& member_1_name,
                                           fxl::RefPtr<Type> member_2_type,
                                           const std::string& member_2_name) {
  auto sc = fxl::MakeRefCounted<Collection>(Symbol::kTagStructureType);
  sc->set_byte_size(member_1_type->byte_size() + member_2_type->byte_size());
  sc->set_assigned_name(struct_name);

  std::vector<LazySymbol> data_members;

  auto member_1 = fxl::MakeRefCounted<DataMember>();
  member_1->set_assigned_name(member_1_name);
  member_1->set_type(LazySymbol(member_1_type));
  member_1->set_member_location(0);
  data_members.push_back(LazySymbol(member_1));

  auto member_2 = fxl::MakeRefCounted<DataMember>();
  member_2->set_assigned_name(member_2_name);
  member_2->set_type(LazySymbol(member_2_type));
  member_2->set_member_location(member_1_type->byte_size());
  data_members.push_back(LazySymbol(member_2));

  sc->set_data_members(std::move(data_members));
  return sc;
}

}  // namespace zxdb
