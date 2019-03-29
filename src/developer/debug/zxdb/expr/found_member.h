// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdint.h>

#include "garnet/bin/zxdb/symbols/data_member.h"
#include "src/lib/fxl/memory/ref_ptr.h"

namespace zxdb {

// The result of finding a member in a collection.
//
// This class consists of a DataMember and an offset within the containing
// object of that DataMember. To actually resolve the value, the containing
// object needs to be known. Typically one would have an object, find a member
// on it (producing a FoundMember), and then use that object and the
// FoundMember to resolve its value.
class FoundMember {
 public:
  FoundMember();
  FoundMember(const DataMember* data_member, uint32_t data_member_offset);

  ~FoundMember();

  const DataMember* data_member() const { return data_member_.get(); }
  fxl::RefPtr<DataMember> data_member_ref() const { return data_member_; }

  uint32_t data_member_offset() const { return data_member_offset_; }

 private:
  // Variable member of the object_var_ that this class represents.
  //
  // NOTE: that this DataMember isn't necessarily a member of the original
  // object that was queried. It could be on a base class. In this case, the
  // offset specified on the DataMember data member will be incorrect since it
  // refers to the offset within its enclosing class. Therefore, one should
  // always use the data_member_offset_ below.
  fxl::RefPtr<DataMember> data_member_;

  // The offset within object_var_ of the data_member_. This will take into
  // account all derived classes.
  uint32_t data_member_offset_ = 0;
};

}  // namespace zxdb
