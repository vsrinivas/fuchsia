// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_SYMBOLS_TYPE_TEST_SUPPORT_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_SYMBOLS_TYPE_TEST_SUPPORT_H_

#include <initializer_list>

#include "src/developer/debug/zxdb/symbols/base_type.h"
#include "src/developer/debug/zxdb/symbols/collection.h"
#include "src/developer/debug/zxdb/symbols/compile_unit.h"
#include "src/developer/debug/zxdb/symbols/modified_type.h"
#include "src/developer/debug/zxdb/symbols/symbol_utils.h"
#include "src/developer/debug/zxdb/symbols/type.h"
#include "src/developer/debug/zxdb/symbols/variant.h"

namespace zxdb {

class BaseType;
class Collection;
class CompileUnit;
class Type;

// Used for declarations that have a name and a type.
using NameAndType = std::pair<std::string, fxl::RefPtr<Type>>;

// Returns a type that can hold 4/8-byte [un]signed integers.
fxl::RefPtr<BaseType> MakeInt16Type();
fxl::RefPtr<BaseType> MakeInt32Type();
fxl::RefPtr<BaseType> MakeUint32Type();
fxl::RefPtr<BaseType> MakeInt64Type();
fxl::RefPtr<BaseType> MakeUint64Type();

fxl::RefPtr<BaseType> MakeDoubleType();

fxl::RefPtr<BaseType> MakeSignedChar8Type();

fxl::RefPtr<BaseType> MakeRustCharType();
fxl::RefPtr<ModifiedType> MakeRustCharPointerType();

// Makes a "char*" as for C strings using a signed 8-bit character.
fxl::RefPtr<ModifiedType> MakeCharPointerType();

// Creates a collection type with the given members.
//
// type_tag is one of DwarfTag::k*Type appropriate for collections (class, struct, union).
//
// For structs and classes, each member will be placed sequentially in the struct starting from
// offset 0, and advancing according to the size of the member. For unions, each member will be at
// offset 0.
fxl::RefPtr<Collection> MakeCollectionType(DwarfTag type_tag, const std::string& struct_name,
                                           std::initializer_list<NameAndType> members);

// Like MakeCollectionType but takes an offset for the first data member to start at. Subsequent
// data members start from there (for unions they will all start from there).
fxl::RefPtr<Collection> MakeCollectionTypeWithOffset(DwarfTag type_tag,
                                                     const std::string& type_name,
                                                     uint32_t first_member_offset,
                                                     std::initializer_list<NameAndType> members);

// Makes a two collections, one a base class of the other, and returns the derived type.
//
// type_tag is one of DwarfTag::k*Type appropriate for collections (class, struct, union).
fxl::RefPtr<Collection> MakeDerivedClassPair(DwarfTag type_tag, const std::string& base_name,
                                             std::initializer_list<NameAndType> base_members,
                                             const std::string& derived_name,
                                             std::initializer_list<NameAndType> derived_members);

// Setting this compile unit as the parent of a symbol will mark it as having the Rust language.
fxl::RefPtr<CompileUnit> MakeRustUnit();

// Makes a Rust variant that can be put into a VariantPart. Rust Variants have a single data member
// that is a struct containing the members passed in which could be empty). So it's got 2 structs.
//
// The variant's single generated data member will be at offset 0 in the containing struct.
// Normally the discriminant in the VariantPart and the data members of each Variant start at
// offset 0 so they overlap! The passed-in members then go inside this struct, and should be
// arranged so they don't overlap the data taken by the discriminant.
fxl::RefPtr<Variant> MakeRustVariant(const std::string& name, std::optional<uint64_t> discriminant,
                                     const std::vector<fxl::RefPtr<DataMember>>& members);

// A rust enum is a collection containing a variant part. The variant part includes a discriminant
// and the variants that it selects from. The caller should ensure the data members in the variants
// and the discriminant don't overlap.
//
// The result will be sized to the largest variant.
fxl::RefPtr<Collection> MakeRustEnum(const std::string& name, fxl::RefPtr<DataMember> discriminant,
                                     const std::vector<fxl::RefPtr<Variant>>& variants);

// Makes a standard rust enum representing the definition:
//
//   enum RustEnum {
//     None,                   // Default
//     Scalar(u32),            // Discriminant = 0
//     Point{x:u32, y:u32},    // Discriminant = 1
//   }
//
// The layout is 12 bytes, the 4 byte discriminant, then the 0-to-8 bytes of values depending on the
// discriminant value (should be padded to 12 total);
//
// Rust doesn't use the "default discriminant" feature of DWARF but we use that here to test our
// interpretation of DWARF. The default discriminant matches any discriminant value that's not
// otherwise explicitly encoded.
fxl::RefPtr<Collection> MakeTestRustEnum();

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_SYMBOLS_TYPE_TEST_SUPPORT_H_
