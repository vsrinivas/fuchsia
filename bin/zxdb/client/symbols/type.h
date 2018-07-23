// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <string>
#include <vector>

#include "garnet/bin/zxdb/client/symbols/symbol.h"

namespace zxdb {

class Type : public Symbol {
 public:
  // Symbol overrides.
  const Type* AsType() const final;
  const std::string& GetAssignedName() const final { return assigned_name_; }

  // The type name that should be shown to the user. This incorporates
  // modifiers like pointers and consts.
  const std::string& GetTypeName() const;

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

  explicit Type(int kind);
  virtual ~Type();

  // Implemented by derived classes to compute the fully qualified type name
  // to be returned by GetTypeName().
  virtual std::string ComputeTypeName() const;

 private:
  std::string assigned_name_;
  uint32_t byte_size_;

  // Lazily computed full type name (including type modifiers). This will be
  // not present if it hasn't been computed yet.
  // TODO(brettw) use std::optional when we can use C++17.
  mutable bool computed_type_name_ = false;
  mutable std::string type_name_;
};

}  // namespace
