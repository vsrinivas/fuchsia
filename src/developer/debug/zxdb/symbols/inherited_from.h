// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "src/developer/debug/zxdb/symbols/symbol.h"

namespace zxdb {

// Defines the relationship between two derived classes. This class will be
// a member of the derived class, and indicates the type of the base class and
// how to get to it.
class InheritedFrom final : public Symbol {
 public:
  // Construct with fxl::MakeRefCounted().

  // Symbol override.
  const InheritedFrom* AsInheritedFrom() const;

  const LazySymbol& from() const { return from_; }

  // This is the DW_AT_data_member_location attribute. In the common case it
  // is a constant that's the offset from the beginning of the derived class to
  // the beginning of the base class.
  //
  // DWARF also allows these to be a location description where the location of
  // the derived class is pushed on the stack, the expression is evaluated, and
  // the result is the location. I have not seen our toolchain generate this
  // type of location so it's not been implemented. If we have a test case,
  // this more complex mode should be supported and tested.
  uint64_t offset() const { return offset_; }

  // We could add the value of the DW_AT_accessibility tag here if needed.

 private:
  FRIEND_REF_COUNTED_THREAD_SAFE(InheritedFrom);
  FRIEND_MAKE_REF_COUNTED(InheritedFrom);

  InheritedFrom(LazySymbol from, uint64_t offset);
  ~InheritedFrom();

  LazySymbol from_;

  uint64_t offset_ = 0;
};

}  // namespace zxdb
