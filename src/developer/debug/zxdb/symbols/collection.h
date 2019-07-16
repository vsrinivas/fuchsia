// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_SYMBOLS_COLLECTION_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_SYMBOLS_COLLECTION_H_

#include <optional>

#include "src/developer/debug/zxdb/symbols/type.h"

namespace zxdb {

// Represents a C/C++ class, struct, or union, or a Rust enum (see the
// variant_part() member).
class Collection final : public Type {
 public:
  // Construct with fxl::MakeRefCounted().

  // Heuristically-derived special type of a collection.
  enum SpecialType {
    // Not known to be special, a normal union, struct, or class.
    kNotSpecial = 0,

    // In Rust enums are represented as collections with a variant part. See variant_part() below.
    //
    //   enum Foo { None, Some(u32), Point{x:i32, y:i32} }
    kRustEnum,

    // A Rust tuple are represented as collections with numbered members of the form "__0", "__1",
    // "__2", etc. See also struct tuple which is a special case of tuple (struct tuples are not
    // considered tuples as far as this enum is concerned). Note that Rust tuples can not have only
    // one item.
    //
    //   let foo = (1, 2);
    kRustTuple,

    // A Rust "tuple struct" acts like a tuple but has a named type that the caller must use. Unlike
    // regular tuples, tuple structs can have one element and are distinct in the type system.
    //
    //   struct Color(i32, i32, i23);
    //   let c = Color(255, 255, 0);
    //
    //   struct FileHandle(u64);
    //   let h = FileHandle(1);
    kRustTupleStruct
  };

  // Symbol overrides.
  const Collection* AsCollection() const override;

  // Data members. These should be DataMember objects.
  const std::vector<LazySymbol>& data_members() const { return data_members_; }
  void set_data_members(std::vector<LazySymbol> d) { data_members_ = std::move(d); }

  // This will be a VariantPart class if there is one defined.
  //
  // Currently this is used only for Rust enums. In this case, the collection will contain one
  // VariantPart (the Variants inside of it will encode the enumerated possibilities) and this
  // collection will have no data_members() in its vector. See the VariantPart declaration for more
  // details.
  //
  // Theoretically DWARF could encode more than one variant part child of a struct but none of our
  // supported compilers or languages do this so we save as a single value.
  const LazySymbol& variant_part() const { return variant_part_; }
  void set_variant_part(const LazySymbol& vp) { variant_part_ = vp; }

  // Classes/structs this one inherits from. This should be a InheritedFrom object.
  //
  // These are in the same order as declared in the symbol file.
  const std::vector<LazySymbol>& inherited_from() const { return inherited_from_; }
  void set_inherited_from(std::vector<LazySymbol> f) { inherited_from_ = std::move(f); }

  // Template parameters if this collection is a template instantiation.
  const std::vector<LazySymbol>& template_params() const { return template_params_; }
  void set_template_params(std::vector<LazySymbol> p) { template_params_ = std::move(p); }

  // Heuristic that attempts to determine whether this collection has special
  // meaning in the current programming language.
  SpecialType GetSpecialType() const;

  // Returns a pointer to either "struct", "class", or "union" depending on the
  // type of this object. This is useful for error messages.
  const char* GetKindString() const;

  // Currently we don't have any notion of member functions because there's no need. That could be
  // added here if necessary (generally the symbols will contain this).

 private:
  FRIEND_REF_COUNTED_THREAD_SAFE(Collection);
  FRIEND_MAKE_REF_COUNTED(Collection);

  Collection(DwarfTag kind, std::string name = std::string());
  virtual ~Collection();

  // Symbol protected overrides.
  std::string ComputeFullName() const override;

  // Backend to GetSpecialType() which adds caching.
  SpecialType ComputeSpecialType() const;

  std::vector<LazySymbol> data_members_;
  LazySymbol variant_part_;
  std::vector<LazySymbol> inherited_from_;
  std::vector<LazySymbol> template_params_;

  // Lazily computed special type of this collection.
  mutable std::optional<SpecialType> special_type_;
};

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_SYMBOLS_COLLECTION_H_
