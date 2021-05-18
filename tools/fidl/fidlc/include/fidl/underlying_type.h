// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TOOLS_FIDL_FIDLC_INCLUDE_FIDL_UNDERLYING_TYPE_H_
#define TOOLS_FIDL_FIDLC_INCLUDE_FIDL_UNDERLYING_TYPE_H_

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
    kPrimitive,
    kString,
    kOther,
  };

  constexpr explicit UnderlyingType(flat::Type::Kind type_kind, bool is_behind_alias)
      : kind_(Kind::kOther), is_behind_alias_(is_behind_alias) {
    switch (type_kind) {
      case flat::Type::Kind::kArray:
        kind_ = Kind::kArray;
        break;
      case flat::Type::Kind::kHandle:
        kind_ = Kind::kHandle;
        break;
      case flat::Type::Kind::kRequestHandle:
        kind_ = Kind::kRequestHandle;
        break;
      case flat::Type::Kind::kTransportSide:
      case flat::Type::Kind::kBox:
        assert(false && "should not be created in the old syntax");
        __builtin_unreachable();
      case flat::Type::Kind::kVector:
        kind_ = Kind::kVector;
        break;
      case flat::Type::Kind::kString:
        kind_ = Kind::kString;
        break;
      case flat::Type::Kind::kPrimitive:
        kind_ = Kind::kPrimitive;
        break;
      default:
        kind_ = Kind::kOther;
    }
  }

  constexpr explicit UnderlyingType(const flat::Decl* decl, bool is_behind_alias)
      : kind_(Kind::kOther), is_behind_alias_(is_behind_alias), maybe_decl_(decl) {
    switch (decl->kind) {
      case flat::Decl::Kind::kProtocol:
        kind_ = Kind::kProtocol;
        break;
      case flat::Decl::Kind::kStruct:
        kind_ = Kind::kStruct;
        break;
      default:
        kind_ = Kind::kOther;
    }
  }

  [[nodiscard]] constexpr Kind kind() const { return kind_; }
  [[nodiscard]] constexpr bool is_behind_alias() const { return is_behind_alias_; }
  [[nodiscard]] constexpr const flat::Decl* maybe_decl() const { return maybe_decl_; }

 private:
  Kind kind_;
  bool is_behind_alias_;
  const flat::Decl* maybe_decl_ = nullptr;
};

}  // namespace fidl::conv

#endif  // TOOLS_FIDL_FIDLC_INCLUDE_FIDL_UNDERLYING_TYPE_H_
