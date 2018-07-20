// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <string>

#include "garnet/bin/zxdb/client/symbols/value.h"

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

  // TODO(brettw) add location information.
  //
  // Simple ones that are always valid look like this:
  //   DW_AT_location (DW_OP_reg5 RDI)
  //
  // Complicated ones with valid ranges look like this:
  //   DW_AT_location:
  //     [0x00000000000ad6be,  0x00000000000ad6c8): DW_OP_reg2 RCX
  //     [0x00000000000ad6c8,  0x00000000000ad780): DW_OP_reg14 R14

 protected:
  FRIEND_REF_COUNTED_THREAD_SAFE(Variable);
  FRIEND_MAKE_REF_COUNTED(Variable);

  explicit Variable(int tag);
  ~Variable();

 private:
};

}  // namespace zxdb
