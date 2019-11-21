// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/symbols/type_test_support.h"

#include "src/developer/debug/zxdb/symbols/base_type.h"
#include "src/developer/debug/zxdb/symbols/collection.h"
#include "src/developer/debug/zxdb/symbols/compile_unit.h"
#include "src/developer/debug/zxdb/symbols/data_member.h"
#include "src/developer/debug/zxdb/symbols/inherited_from.h"
#include "src/developer/debug/zxdb/symbols/modified_type.h"
#include "src/developer/debug/zxdb/symbols/variant.h"
#include "src/developer/debug/zxdb/symbols/variant_part.h"
#include "src/lib/fxl/strings/string_printf.h"

namespace zxdb {

fxl::RefPtr<BaseType> MakeInt16Type() {
  return fxl::MakeRefCounted<BaseType>(BaseType::kBaseTypeSigned, 2, "int16_t");
}

fxl::RefPtr<BaseType> MakeInt32Type() {
  return fxl::MakeRefCounted<BaseType>(BaseType::kBaseTypeSigned, 4, "int32_t");
}

fxl::RefPtr<BaseType> MakeUint32Type() {
  return fxl::MakeRefCounted<BaseType>(BaseType::kBaseTypeUnsigned, 4, "uint32_t");
}

fxl::RefPtr<BaseType> MakeInt64Type() {
  return fxl::MakeRefCounted<BaseType>(BaseType::kBaseTypeSigned, 8, "int64_t");
}

fxl::RefPtr<BaseType> MakeUint64Type() {
  return fxl::MakeRefCounted<BaseType>(BaseType::kBaseTypeUnsigned, 8, "uint64_t");
}

fxl::RefPtr<BaseType> MakeDoubleType() {
  return fxl::MakeRefCounted<BaseType>(BaseType::kBaseTypeFloat, 8, "double");
}

fxl::RefPtr<BaseType> MakeSignedChar8Type() {
  return fxl::MakeRefCounted<BaseType>(BaseType::kBaseTypeSignedChar, 1, "char");
}

fxl::RefPtr<BaseType> MakeRustCharType() {
  auto type = fxl::MakeRefCounted<BaseType>(BaseType::kBaseTypeUnsignedChar, 4, "char");
  type->set_parent(UncachedLazySymbol::MakeUnsafe(MakeRustUnit()));
  return type;
}

fxl::RefPtr<ModifiedType> MakeRustCharPointerType() {
  auto char_type = MakeRustCharType();
  auto mod = fxl::MakeRefCounted<ModifiedType>(DwarfTag::kPointerType, char_type);
  mod->set_parent(UncachedLazySymbol::MakeUnsafe(char_type->parent().Get()));
  return mod;
}

fxl::RefPtr<ModifiedType> MakeCharPointerType() {
  return fxl::MakeRefCounted<ModifiedType>(DwarfTag::kPointerType, MakeSignedChar8Type());
}

fxl::RefPtr<Collection> MakeCollectionType(DwarfTag type_tag, const std::string& type_name,
                                           std::initializer_list<NameAndType> members) {
  return MakeCollectionTypeWithOffset(type_tag, type_name, 0, std::move(members));
}

fxl::RefPtr<Collection> MakeCollectionTypeWithOffset(DwarfTag type_tag,
                                                     const std::string& type_name,
                                                     uint32_t first_member_offset,
                                                     std::initializer_list<NameAndType> members) {
  auto result = fxl::MakeRefCounted<Collection>(type_tag, type_name);

  uint32_t offset = first_member_offset;

  uint32_t max_union_member_size = 0;  // For computing union sizes.

  std::vector<LazySymbol> data_members;
  for (const auto& [name, type] : members) {
    auto member = fxl::MakeRefCounted<DataMember>();
    member->set_assigned_name(name);
    member->set_type(type);
    member->set_member_location(offset);
    data_members.emplace_back(member);

    if (type_tag == DwarfTag::kUnionType)
      max_union_member_size = std::max(max_union_member_size, type->byte_size());
    else
      offset += type->byte_size();
  }

  if (type_tag == DwarfTag::kUnionType)
    result->set_byte_size(max_union_member_size);  // Unions are the max size of each member.
  else
    result->set_byte_size(offset);  // Adds up all member offsets.

  result->set_data_members(std::move(data_members));
  return result;
}

fxl::RefPtr<Collection> MakeDerivedClassPair(DwarfTag type_tag, const std::string& base_name,
                                             std::initializer_list<NameAndType> base_members,
                                             const std::string& derived_name,
                                             std::initializer_list<NameAndType> derived_members) {
  auto base = MakeCollectionTypeWithOffset(type_tag, base_name, 0, std::move(base_members));

  // Leave room at the beginning of |derived| for the base class.
  auto derived = MakeCollectionTypeWithOffset(type_tag, derived_name, base->byte_size(),
                                              std::move(derived_members));

  derived->set_inherited_from({LazySymbol(fxl::MakeRefCounted<InheritedFrom>(base, 0))});
  return derived;
}

fxl::RefPtr<CompileUnit> MakeRustUnit() {
  auto unit = fxl::MakeRefCounted<CompileUnit>();
  unit->set_language(DwarfLang::kRust);
  return unit;
}

fxl::RefPtr<Variant> MakeRustVariant(const std::string& name, std::optional<uint64_t> discriminant,
                                     const std::vector<fxl::RefPtr<DataMember>>& members) {
  // For Rust triggering to happen the compilation unit must be set. The easiest way to do this is
  // to set the compilation unit as the parent.  This doesn't produce a strictly valid structure
  // since the parents won't be "right" when traversing the symbol hierarchy upward, but that's not
  // been necessary so far.
  //
  // TODO(brettw) have a better way to set the language for symbols.
  auto unit = MakeRustUnit();

  // Pick the byte size to be the size after the last member.
  uint32_t byte_size = 0;
  if (members.size() > 0) {
    byte_size =
        members.back()->member_location() + members.back()->type().Get()->AsType()->byte_size();
  }

  // The single member of the variant has a type name of the variant name.  This type holds all the
  // members passed in.
  auto variant_member_type = fxl::MakeRefCounted<Collection>(DwarfTag::kStructureType, name);
  variant_member_type->set_parent(UncachedLazySymbol::MakeUnsafe(unit));
  variant_member_type->set_byte_size(byte_size);

  std::vector<LazySymbol> lazy_members;
  for (const auto& member : members) {
    member->set_parent(UncachedLazySymbol::MakeUnsafe(unit));
    lazy_members.emplace_back(member);
  }
  variant_member_type->set_data_members(std::move(lazy_members));

  // This data member in the variant contains the structure above. We assume it starts at offset 0
  // in the containing struct.
  auto variant_data = fxl::MakeRefCounted<DataMember>(name, variant_member_type, 0);
  variant_data->set_parent(UncachedLazySymbol::MakeUnsafe(unit));

  auto var =
      fxl::MakeRefCounted<Variant>(discriminant, std::vector<LazySymbol>{LazySymbol(variant_data)});
  var->set_parent(UncachedLazySymbol::MakeUnsafe(unit));
  return var;
}

fxl::RefPtr<Collection> MakeRustEnum(const std::string& name, fxl::RefPtr<DataMember> discriminant,
                                     const std::vector<fxl::RefPtr<Variant>>& variants) {
  auto unit = MakeRustUnit();
  uint32_t byte_size = 0;

  std::vector<LazySymbol> lazy_variants;
  for (const auto& var : variants) {
    // Pick the size based on the largest variant
    if (!var->data_members().empty()) {
      const DataMember* last_member = var->data_members().back().Get()->AsDataMember();
      FXL_DCHECK(last_member);  // ASsume test code has set up properly.
      uint32_t var_byte_size =
          last_member->member_location() + last_member->type().Get()->AsType()->byte_size();
      if (var_byte_size > byte_size)
        byte_size = var_byte_size;
    }

    lazy_variants.emplace_back(var);
  }

  auto variant_part = fxl::MakeRefCounted<VariantPart>(discriminant, std::move(lazy_variants));
  variant_part->set_parent(UncachedLazySymbol::MakeUnsafe(unit));

  auto collection = fxl::MakeRefCounted<Collection>(DwarfTag::kStructureType, name);
  collection->set_variant_part(variant_part);
  collection->set_byte_size(byte_size);
  collection->set_parent(UncachedLazySymbol::MakeUnsafe(unit));

  return collection;
}

fxl::RefPtr<Collection> MakeTestRustEnum() {
  // Say "None is the default variant so has no discriminant (anything other than these values will
  // match "none".
  const uint64_t kScalarDiscriminant = 0;
  const uint64_t kPointDiscriminant = 1;

  // Set as parent to indicate this is a Rust value.
  auto unit = MakeRustUnit();

  // This 4-byte value encodes the discriminant value which indicates which
  // variant is valid. It's at offset 0 in the struct,
  auto uint32_type = MakeInt32Type();
  uint32_type->set_parent(UncachedLazySymbol::MakeUnsafe(unit));

  auto discriminant = fxl::MakeRefCounted<DataMember>(std::string(), uint32_type, 0);

  // None variant.
  auto none_variant = MakeRustVariant("None", std::nullopt, {});

  // Scalar variant. The member is named with "__0" like Rust does. All the members must start after
  // the discriminant above (4 bytes).
  auto scalar_data = fxl::MakeRefCounted<DataMember>("__0", uint32_type, 4);
  auto scalar_variant = MakeRustVariant("Scalar", kScalarDiscriminant,
                                        std::vector<fxl::RefPtr<DataMember>>{scalar_data});

  // Point variant. The two members start after the discriminant (4 bytes).
  auto x_data = fxl::MakeRefCounted<DataMember>("x", uint32_type, 4);
  auto y_data = fxl::MakeRefCounted<DataMember>("y", uint32_type, 8);
  auto point_variant = MakeRustVariant("Point", kPointDiscriminant,
                                       std::vector<fxl::RefPtr<DataMember>>{x_data, y_data});

  // Structure that contains the variants. It has a variant_part and no data.
  auto rust_enum =
      MakeRustEnum("RustEnum", discriminant, {none_variant, scalar_variant, point_variant});
  rust_enum->set_parent(UncachedLazySymbol::MakeUnsafe(MakeRustUnit()));
  return rust_enum;
}

fxl::RefPtr<Collection> MakeTestRustTuple(const std::string& name,
                                          const std::vector<fxl::RefPtr<Type>>& members) {
  auto coll = fxl::MakeRefCounted<Collection>(DwarfTag::kStructureType, name);
  coll->set_parent(UncachedLazySymbol::MakeUnsafe(MakeRustUnit()));

  uint32_t offset = 0;
  std::vector<LazySymbol> data_members;
  for (size_t i = 0; i < members.size(); i++) {
    auto& type = members[i];
    auto data = fxl::MakeRefCounted<DataMember>(fxl::StringPrintf("__%zu", i), type, offset);

    data_members.emplace_back(std::move(data));
    offset += type->byte_size();
  }

  coll->set_byte_size(offset);
  coll->set_data_members(std::move(data_members));
  return coll;
}

}  // namespace zxdb
