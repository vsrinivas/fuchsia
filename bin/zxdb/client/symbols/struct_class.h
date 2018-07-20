// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "garnet/bin/zxdb/client/symbols/type.h"

namespace zxdb {

// Represents a C++ class or a struct.
class StructClass final : public Type {
 public:
  // Construct with fxl::MakeRefCounted().

  // Symbol overrides.
  const StructClass* AsStructClass() const;

  // Data members.
  const std::vector<LazySymbol>& data_members() const { return data_members_; }
  void set_data_members(std::vector<LazySymbol> d) { data_members_ = std::move(d); }

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
