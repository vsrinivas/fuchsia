// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <string>
#include <vector>

#include "src/developer/debug/zxdb/symbols/symbol.h"

namespace zxdb {

class Type : public Symbol {
 public:
  // Symbol overrides.
  const Type* AsType() const final;
  const std::string& GetAssignedName() const { return assigned_name_; }

  // Returns the type with no "const", "volatile", or similar modifiers that
  // don't affect the stored data, and expand typedef and using statements.
  //
  // It is on the Type class rather than the ModifiedType class so that calling
  // code can unconditionally call type->GetConcreteType()->byte_size() or
  // other functions to work with the type.
  virtual const Type* GetConcreteType() const;

  // The name assigned in the DWARF file. This will be empty for modified
  // types (Which usually have no assigned name). See
  // Symbol::GetAssignedName).
  void set_assigned_name(std::string n) { assigned_name_ = std::move(n); }

  // For forward-defines where the size of the structure is not known, the
  // byte size will be 0.
  uint32_t byte_size() const { return byte_size_; }
  void set_byte_size(uint32_t bs) { byte_size_ = bs; }

 protected:
  FRIEND_REF_COUNTED_THREAD_SAFE(Type);
  FRIEND_MAKE_REF_COUNTED(Type);

  explicit Type(DwarfTag kind);
  virtual ~Type();

 private:
  std::string assigned_name_;
  uint32_t byte_size_ = 0;
};

}  // namespace zxdb
