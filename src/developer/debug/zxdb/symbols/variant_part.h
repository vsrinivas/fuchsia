// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_SYMBOLS_VARIANT_PART_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_SYMBOLS_VARIANT_PART_H_

#include "src/developer/debug/zxdb/symbols/symbol.h"

namespace zxdb {

class Variant;

// In DWARF a collection (struct or class) can have part of it that's a variant (basically a tagged
// union).
//
//  - C++ doesn't use these.
//  - Rust uses them for its enums which have a single value of a known type (though possibly a
//    tuple) associated with each enum value. In this case there will be no non-variant parts of the
//    structure.
//
// VariantParts can have a "discriminant" which is a variable in the structure whose value indicates
// which of the variants is currently active. DWARF doesn't require this discriminant but currently
// we do since our only case (Rust) generates them.
//
// The discriminant is a DataMember that holds a value. This is one of the "discr_value" values from
// the variants and identifies which Variant this VariantPart currently contains.
class VariantPart final : public Symbol {
 public:
  // Construct with fxl::MakeRefCounted().

  // Symbol overrides.
  const VariantPart* AsVariantPart() const override { return this; }

  // DataMember whose value indicates which variant is active. Most callers will want only
  // GetVariant(). The offsets of the data member will be from the structure containing this
  // VariantPart.
  const LazySymbol& discriminant() const { return discriminant_; }

  // All variants described. Most callers will want only GetVariant().
  const std::vector<LazySymbol>& variants() const { return variants_; }

 private:
  FRIEND_REF_COUNTED_THREAD_SAFE(VariantPart);
  FRIEND_MAKE_REF_COUNTED(VariantPart);

  VariantPart(const LazySymbol& discriminant, std::vector<LazySymbol> variants);
  virtual ~VariantPart();

  LazySymbol discriminant_;
  std::vector<LazySymbol> variants_;
};

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_SYMBOLS_VARIANT_PART_H_
