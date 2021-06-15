// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_SYMBOLS_VARIABLE_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_SYMBOLS_VARIABLE_H_

#include <string>

#include "src/developer/debug/zxdb/symbols/value.h"
#include "src/developer/debug/zxdb/symbols/variable_location.h"

namespace zxdb {

// A variable is a value that can exist on the stack or in memory (it has a DWARF "location"). This
// includes "variable" and "formal parameter" types.  Not to be confused with DataMembers which are
// located via an offset from their containing struct or class.
class Variable : public Value {
 public:
  // Construct with fxl::MakeRefCounted().

  // Holds the location of the variable value if it has a memory or register value. The symbols
  // could also be encoded to express a constant value as a DWARF expression which will be stored
  // here.
  //
  // If value is constant and optimized out, Value::const_value() (on the base class) may contain
  // the literal value instead and there will be no location.
  //
  // Generally one should check const_value().has_value() and fall back to location() if not.
  const VariableLocation& location() const { return location_; }
  void set_location(VariableLocation loc) { location_ = std::move(loc); }

 protected:
  FRIEND_REF_COUNTED_THREAD_SAFE(Variable);
  FRIEND_MAKE_REF_COUNTED(Variable);

  explicit Variable(DwarfTag tag);
  Variable(DwarfTag tag, const std::string& assigned_name, LazySymbol type,
           VariableLocation location);
  ~Variable();

  // Symbol overrides.
  const Variable* AsVariable() const override;

 private:
  VariableLocation location_;
};

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_SYMBOLS_VARIABLE_H_
