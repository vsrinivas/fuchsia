// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <string>

#include "garnet/bin/zxdb/client/symbols/value.h"
#include "garnet/bin/zxdb/client/symbols/variable_location.h"

namespace zxdb {

// A variable is a value that can exist on the stack or in memory (it has a
// DWARF "location"). This includes "variable" and "formal parameter" types.
// Not to be confused with data members which are located via an offset from
// their containing struct or class.
class Variable : public Value {
 public:
  // Construct with fxl::MakeRefCounted().

  // Symbol overrides.
  const Variable* AsVariable() const override;

  const VariableLocation& location() const { return location_; }
  void set_location(VariableLocation loc) { location_ = std::move(loc); }

 protected:
  FRIEND_REF_COUNTED_THREAD_SAFE(Variable);
  FRIEND_MAKE_REF_COUNTED(Variable);

  explicit Variable(int tag);
  ~Variable();

 private:
  VariableLocation location_;
};

}  // namespace zxdb
