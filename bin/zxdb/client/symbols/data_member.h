// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "garnet/bin/zxdb/client/symbols/value.h"

namespace zxdb {

// Represents a data member in a class. Not to be confused with function
// parameters and local variables which are represented by a Variable.
//
// The type and name come from the Value base class.
class DataMember final : public Value {
 public:
  // Construct with fxl::MakeRefCounted().

  // Symbol overrides.
  const DataMember* AsDataMember() const;

  // This currently doesn't reference the enclosing block (which will be the
  // class or struct this is a member of) because we normally work down the
  // other way. This information could be added if needed.

  // The byte offset from the containing class or struct of this data member.
  uint32_t member_location() const { return member_location_; }
  void set_member_location(uint32_t m) { member_location_ = m; }

 private:
  FRIEND_REF_COUNTED_THREAD_SAFE(DataMember);
  FRIEND_MAKE_REF_COUNTED(DataMember);

  DataMember();
  ~DataMember();

  uint32_t member_location_ = 0;
};

}  // namespace zxdb
