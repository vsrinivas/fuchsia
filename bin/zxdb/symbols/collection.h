// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "garnet/bin/zxdb/symbols/type.h"

namespace zxdb {

// Represents a C/C++ class, struct, or union.
class Collection final : public Type {
 public:
  // Construct with fxl::MakeRefCounted().

  // Symbol overrides.
  const Collection* AsCollection() const;

  // Data members. These should be of Value types.
  const std::vector<LazySymbol>& data_members() const { return data_members_; }
  void set_data_members(std::vector<LazySymbol> d) {
    data_members_ = std::move(d);
  }

  // Classes/structs this one inherits from. These are in the same order as
  // declared in the symbol file.
  const std::vector<LazySymbol>& inherited_from() const {
    return inherited_from_;
  }
  void set_inherited_from(std::vector<LazySymbol> f) {
    inherited_from_ = std::move(f);
  }

  // Returns a pointer to either "struct", "class", or "union" depending on the
  // type of this object. This is useful for error messages.
  const char* GetKindString() const;

  // Currently we don't have any notion of member functions because there's
  // no need. That could be added here if necessary (generally the symbols
  // will contain this).

 private:
  FRIEND_REF_COUNTED_THREAD_SAFE(Collection);
  FRIEND_MAKE_REF_COUNTED(Collection);

  explicit Collection(int kind);
  virtual ~Collection();

  std::vector<LazySymbol> data_members_;
  std::vector<LazySymbol> inherited_from_;
};

}  // namespace zxdb
