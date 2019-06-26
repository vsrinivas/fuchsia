// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_SYMBOLS_DATA_MEMBER_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_SYMBOLS_DATA_MEMBER_H_

#include "src/developer/debug/zxdb/symbols/value.h"

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

  // The byte offset from the containing class or struct of this data member.
  uint32_t member_location() const { return member_location_; }
  void set_member_location(uint32_t m) { member_location_ = m; }

 private:
  FRIEND_REF_COUNTED_THREAD_SAFE(DataMember);
  FRIEND_MAKE_REF_COUNTED(DataMember);

  DataMember();
  DataMember(const std::string& assigned_name, LazySymbol type, uint32_t member_loc);
  ~DataMember();

  uint32_t member_location_ = 0;
};

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_SYMBOLS_DATA_MEMBER_H_
