// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "garnet/bin/zxdb/symbols/type.h"

namespace zxdb {

// Represents a C++ class or a struct.
class StructClass final : public Type {
 public:
  // Construct with fxl::MakeRefCounted().

  // Symbol overrides.
  const StructClass* AsStructClass() const;

  // Data members. These should be of Value types.
  const std::vector<LazySymbol>& data_members() const { return data_members_; }
  void set_data_members(std::vector<LazySymbol> d) {
    data_members_ = std::move(d);
  }

  // Returns a pointer to either "struct" or "class" depending on the type of
  // this object. This is useful for error messages.
  const char* GetStructOrClassString() const;

  // Currently we don't have any notion of member functions because there's
  // no need. That could be added here if necessary (generally the symbols
  // will contain this).

 private:
  FRIEND_REF_COUNTED_THREAD_SAFE(StructClass);
  FRIEND_MAKE_REF_COUNTED(StructClass);

  explicit StructClass(int kind);
  virtual ~StructClass();

  std::vector<LazySymbol> data_members_;
};

}  // namespace zxdb
