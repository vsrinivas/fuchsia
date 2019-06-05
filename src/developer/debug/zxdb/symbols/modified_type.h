// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_SYMBOLS_MODIFIED_TYPE_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_SYMBOLS_MODIFIED_TYPE_H_

#include "src/developer/debug/zxdb/symbols/type.h"

namespace zxdb {

// A DWARF type modifier is something that applies to an underlying type.
// If you want to declare a type for "int*", you would first declare a base
// type for "int", and then declare a pointer modifier that references the
// "int" record.
//
// We also count typedefs as type modifiers since they apply a new name to a
// type in the same manner.
//
// We also count imported declarations are type modifiers. These are "using"
// statements. They also reference an underlying type but won't have a name.
// In this case, the name comes from the modified type but the namespace
// comes from the surrounding context of the ModifiedType.
class ModifiedType final : public Type {
 public:
  // Type/Symbol overrides.
  const ModifiedType* AsModifiedType() const override;
  const Type* StripCVT() const override;

  // The underlying modified type. Note that there is no setter for this, it
  // must be supplied in the constructor since the setter will decode the
  // modified type if necessary to compute its size.
  //
  // This may be null. This means the modified type is "void" (e.g. a pointer
  // modifying nothing is a void*, and a typedef of nothing is a typedef for
  // void).
  const LazySymbol& modified() const { return modified_; }

  // Returns true if this modified type is a modification of "void", e.g.
  // "void*".
  bool ModifiesVoid() const;

 private:
  FRIEND_REF_COUNTED_THREAD_SAFE(ModifiedType);
  FRIEND_MAKE_REF_COUNTED(ModifiedType);

  ModifiedType(DwarfTag kind, LazySymbol modified);
  ~ModifiedType() override;

  // Symbol protected overrides.
  std::string ComputeFullName() const override;
  Identifier ComputeIdentifier() const override;

  LazySymbol modified_;
};

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_SYMBOLS_MODIFIED_TYPE_H_
