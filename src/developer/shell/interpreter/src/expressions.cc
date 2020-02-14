// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/shell/interpreter/src/expressions.h"

#include <ostream>

namespace shell {
namespace interpreter {

void IntegerLiteral::Dump(std::ostream& os) const {
  if (negative_) {
    os << '-';
  }
  os << absolute_value_;
}

}  // namespace interpreter
}  // namespace shell
