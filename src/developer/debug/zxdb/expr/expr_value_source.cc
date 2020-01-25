// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/expr/expr_value_source.h"

#include "src/lib/fxl/logging.h"

namespace zxdb {

// static
const char* ExprValueSource::TypeToString(Type t) {
  switch (t) {
    case Type::kTemporary:
      return "temporary";
    case Type::kMemory:
      return "memory";
    case Type::kRegister:
      return "register";
    case Type::kConstant:
      return "constant";
    case Type::kComposite:
      return "composite";
  }
  return "unknown";
}

uint128_t ExprValueSource::SetBits(uint128_t existing, uint128_t new_value) const {
  FXL_DCHECK(is_bitfield());

  uint128_t value = new_value << bit_shift();

  // The mask to write the value at this location, taking into account both the bit size and bit
  // shift. The bits to write to will be set to 1.
  uint128_t write_mask = (static_cast<uint128_t>(1) << bit_size()) - 1;
  write_mask <<= bit_shift();

  existing &= ~write_mask;  // Zero the dest bits we'll write.
  return existing | (value & write_mask);
}

}  // namespace zxdb
