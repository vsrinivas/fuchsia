// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_TOOLS_FIDL_INCLUDE_BUILTIN_H_
#define ZIRCON_TOOLS_FIDL_INCLUDE_BUILTIN_H_

// UnderlyingType stores the builtin type information for a given FIDL construct.
// It basically maps to the FIDL keyword used to define the type (eg, "struct,"
// "table," "vector," "request," etc).  Since this type-space is spread across
// two enum lists in the flat_ast library, the UnderlyingType class unifies them
// into a single object.
#include <fidl/flat_ast.h>

namespace fidl::conv {

class UnderlyingType {
 public:
  enum struct Kind {
    kArray,
    kHandle,
    kProtocol,
    kRequestHandle,
    kStruct,
    kVector,
    kOther,
  };

  constexpr explicit UnderlyingType(flat::Type::Kind type_kind, bool is_behind_alias)
      : kind_(Kind::kOther),
        is_behind_alias_(is_behind_alias) {
    switch(type_kind) {
      case flat::Type::Kind::kArray:
        kind_ = Kind::kArray;
        break;
      case flat::Type::Kind::kHandle:
        kind_ = Kind::kHandle;
        break;
      case flat::Type::Kind::kRequestHandle:
        kind_ = Kind::kRequestHandle;
        break;
      case flat::Type::Kind::kVector:
        kind_ = Kind::kVector;
        break;
      default:
        kind_ = Kind::kOther;
    }
  }

  constexpr explicit UnderlyingType(flat::Decl::Kind decl_kind, bool is_behind_alias)
      : kind_(Kind::kOther),
        is_behind_alias_(is_behind_alias) {
    switch(decl_kind) {
      case flat::Decl::Kind::kProtocol:
        kind_ = Kind::kProtocol;
        break;
      case flat::Decl::Kind::kStruct:
        kind_ = Kind::kStruct;
        break;
      default:
        kind_ = Kind::kOther;
    }}

  [[nodiscard]] constexpr Kind kind() const { return kind_; }
  [[nodiscard]] constexpr bool is_behind_alias() const { return is_behind_alias_; }

 private:
  Kind kind_;
  bool is_behind_alias_;
};

}  // namespace fidl::conv

#endif //ZIRCON_TOOLS_FIDL_INCLUDE_BUILTIN_H_
