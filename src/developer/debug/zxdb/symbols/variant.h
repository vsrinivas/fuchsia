// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_SYMBOLS_VARIANT_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_SYMBOLS_VARIANT_H_

#include <optional>
#include <vector>

#include "src/developer/debug/zxdb/symbols/symbol.h"

namespace zxdb {

// A variant is one possible value of a "variant part".
//
// Each Variant contains a discriminant value which is a selector for this in
// the containing VariantPart, and the set of data inside it.
//
// See VariantPart for a full description.
class Variant final : public Symbol {
 public:
  // Construct with fxl::MakeRefCounted().

  // Symbol overrides.
  const Variant* AsVariant() const override { return this; }

  // The disciminant value associated with this variant. See VariantPart.
  //
  // The discriminant value may be unset which indicates that this variant is
  // the default one.
  //
  // DWARF discriminant values can be either signed or unsigned, according to
  // the type associated with the discriminant data member in the VariantPart.
  // This makes it complicated do handle because the full type of the
  // VariantPart needs to be understood just to properly parse the Variant out
  // of the file.
  //
  // Since our only current use of these is Rust which always uses unsigned
  // disciminants, we also assume unsigned here.
  //
  // If in the future we need to support signed disciminants, we could
  // sign-extend the values during decode so that internally we always deal
  // with unsigned types.
  const std::optional<uint64_t>& discr_value() const { return discr_value_; }

  // Data members. These should be DataMember objects. The offsets of the data
  // members will be from the structure containing the VariantPart.
  //
  // As of this writing, Rust (our only use-case for this) generates variants
  // with exactly one data member. If Rust has:
  //
  //   enum MyEnum {
  //     Foo,
  //     Bar(i32),
  //   }
  //
  // DWARF will define two structure types "MyEnum::Foo" (with no members) and
  // "MyEnum::Bar" (with one member) and each variant's data members will
  // contain a DataMember of that type. The "name" of these data members will
  // match the type ("Foo" and "Bar" in this example).
  const std::vector<LazySymbol>& data_members() const { return data_members_; }

 private:
  FRIEND_REF_COUNTED_THREAD_SAFE(Variant);
  FRIEND_MAKE_REF_COUNTED(Variant);

  Variant(std::optional<uint64_t> discr_value, std::vector<LazySymbol> data_members);
  virtual ~Variant();

  std::optional<uint64_t> discr_value_;
  std::vector<LazySymbol> data_members_;
};

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_SYMBOLS_VARIANT_H_
