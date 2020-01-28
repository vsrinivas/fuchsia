// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_EXPR_FOUND_MEMBER_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_EXPR_FOUND_MEMBER_H_

#include <stdint.h>

#include "src/developer/debug/zxdb/symbols/data_member.h"
#include "src/developer/debug/zxdb/symbols/inheritance_path.h"
#include "src/lib/fxl/memory/ref_ptr.h"

namespace zxdb {

// The result of finding a member in a collection.
//
// This class consists of a DataMember and how to find it from a given class.
//
// To actually resolve the value when the data_member is not static, the containing object needs to
// be known. Typically one would have an object, find a member on it (producing a FoundMember), and
// then use that object and the FoundMember to resolve its value.
//
// If the data member is static data_member()->is_external() will be set.
class FoundMember {
 public:
  FoundMember();

  // Constructs from a data member on a class with no inheritance. This means the DataMember must be
  // a direct member of the collection it's referring to.
  //
  // The collection can be null for static data members.
  FoundMember(const Collection* collection, const DataMember* data_member);

  // Constructs from a data member and an object path.
  FoundMember(InheritancePath path, const DataMember* data_member);

  ~FoundMember();

  bool is_null() const { return !data_member_; }

  // The inheritance path is used to find the member data only for nonstatic members.
  //
  // Static members will have data_member()->is_external() set and the expression will not depend on
  // the object. In this case, the object_path() will be empty.
  //
  // This path can contain synthetic items not strictly in the inheritance tree in the case of
  // anonymous structs or unions. An InheritedFrom object will be synthesized to represent the
  // offset of the anonymous struct/union in its enclosing collection.
  const InheritancePath& object_path() const { return object_path_; }

  // Variable member of the object_var_ that this class represents. Can be null to represent
  // "not found". Check is_null().
  //
  // NOTE: that this DataMember isn't necessarily a member of the original object that was queried.
  // It could be on a base class. In this case, the offset specified on the DataMember data member
  // will be incorrect since it refers to the offset within its enclosing class. Therefore, one
  // should always use the data_member_offset_ below.
  const DataMember* data_member() const { return data_member_.get(); }
  fxl::RefPtr<DataMember> data_member_ref() const { return data_member_; }

  // Helper to extract the offset of the data member in the class. This can fail if there is virtual
  // inheritance or the data member is static (in both cases the data member isn't at a fixed
  // offset from the collection).
  std::optional<uint32_t> GetDataMemberOffset() const;

 private:
  // See documentation above.
  InheritancePath object_path_;
  fxl::RefPtr<DataMember> data_member_;
};

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_EXPR_FOUND_MEMBER_H_
