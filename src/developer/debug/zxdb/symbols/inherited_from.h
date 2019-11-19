// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_SYMBOLS_INHERITED_FROM_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_SYMBOLS_INHERITED_FROM_H_

#include "src/developer/debug/zxdb/symbols/symbol.h"

namespace zxdb {

// Defines the relationship between two derived classes. This class will be a member of the derived
// class, and indicates the type of the base class and how to get to it.
//
// DWARF has two ways of encoding this.
//
//  - The location can be a constant in which case this means it's an offset from the containing
//    struct's beginning. This is the most common case.
//
//  - The location can be an expression. In this case the derived class' offset is pushed on the
//    stack and the expression is evaluated to get the address of the base class. This is used for
//    C++ virtual inheritance where the pointer to the base class is stored near the beginning
//    of the class.
class InheritedFrom final : public Symbol {
 public:
  // Construct with fxl::MakeRefCounted().

  // How this location is expressed. See class-level comment above.
  enum Kind {
    kConstant,    // Expressed as an offset() from the derived class.
    kExpression,  // Expressed as a location_expression().
  };

  // Symbol override.
  const InheritedFrom* AsInheritedFrom() const;

  Kind kind() const { return kind_; }

  const LazySymbol& from() const { return from_; }

  // This is the DW_AT_data_member_location attribute for constant values. This will be valid
  // when kind() == kConstant. See class-level comment above.
  uint64_t offset() const { return offset_; }

  // This is the DW_AT_data_member_location attribute for general expression locations. This will be
  // valid when kind() == kExpression. See class-level comment above.
  std::vector<uint8_t> location_expression() const { return location_expression_; }

  // We could add the value of the DW_AT_accessibility for public/private and DW_TAG_virtuality for
  // virtual inheritance.

 private:
  FRIEND_REF_COUNTED_THREAD_SAFE(InheritedFrom);
  FRIEND_MAKE_REF_COUNTED(InheritedFrom);

  InheritedFrom(LazySymbol from, uint64_t offset);
  InheritedFrom(LazySymbol from, std::vector<uint8_t> expr);
  ~InheritedFrom();

  Kind kind_ = kConstant;
  LazySymbol from_;

  uint64_t offset_ = 0;
  std::vector<uint8_t> location_expression_;
};

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_SYMBOLS_INHERITED_FROM_H_
