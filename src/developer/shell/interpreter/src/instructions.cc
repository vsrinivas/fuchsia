// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/shell/interpreter/src/instructions.h"

#include <ostream>

#include "src/developer/shell/interpreter/src/interpreter.h"

namespace shell {
namespace interpreter {

void VariableDefinition::Dump(std::ostream& os) const {
  os << (is_mutable_ ? "var " : "const ") << name_;
  if (type_ != nullptr) {
    os << ": " << *type_;
  }
  if (initial_value_ != nullptr) {
    os << " = " << *initial_value_;
  }
  os << '\n';
}

}  // namespace interpreter
}  // namespace shell
