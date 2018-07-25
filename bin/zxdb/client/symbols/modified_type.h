// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "garnet/bin/zxdb/client/symbols/type.h"

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
  // Returns true if the given DWARF tag is a type modifier.
  static bool IsTypeModifierTag(int tag);

  // Type/Symbol overrides.
  const ModifiedType* AsModifiedType() const override;

  // The underlying modified type.
  const LazySymbol& modified() const { return modified_; }
  void set_modified(const LazySymbol& m) { modified_ = m; }

 private:
  FRIEND_REF_COUNTED_THREAD_SAFE(ModifiedType);
  FRIEND_MAKE_REF_COUNTED(ModifiedType);

  explicit ModifiedType(int kind);
  ~ModifiedType() override;

  // Symbol protected overrides.
  std::string ComputeFullName() const override;

  LazySymbol modified_;
};

}  // namespace zxdb
