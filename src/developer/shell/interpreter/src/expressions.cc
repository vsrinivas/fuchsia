// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/shell/interpreter/src/expressions.h"

#include <ostream>

#include "src/developer/shell/interpreter/src/nodes.h"
#include "src/developer/shell/interpreter/src/types.h"

namespace shell {
namespace interpreter {

std::unique_ptr<Type> Expression::GetType() const { return std::make_unique<TypeUndefined>(); }

std::unique_ptr<Type> IntegerLiteral::GetType() const { return std::make_unique<TypeInteger>(); }

void IntegerLiteral::Dump(std::ostream& os) const {
  if (negative_) {
    os << '-';
  }
  os << absolute_value_;
}

}  // namespace interpreter
}  // namespace shell
