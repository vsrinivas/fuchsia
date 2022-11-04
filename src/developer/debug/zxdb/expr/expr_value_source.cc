// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/expr/expr_value_source.h"

#include <lib/syslog/cpp/macros.h>

#include "src/developer/debug/zxdb/expr/local_expr_value.h"

namespace zxdb {

// Constructors and destructors need to be out-of-line so they can get the definition of
// LocalExprValue in this file.

ExprValueSource::ExprValueSource(Type type) : type_(type) {}

ExprValueSource::ExprValueSource(uint64_t address, uint32_t bit_size, uint32_t bit_shift)
    : type_(Type::kMemory), address_(address), bit_size_(bit_size), bit_shift_(bit_shift) {}

ExprValueSource::ExprValueSource(debug::RegisterID id, uint32_t bit_size, uint32_t bit_shift)
    : type_(Type::kRegister), register_id_(id), bit_size_(bit_size), bit_shift_(bit_shift) {}

ExprValueSource::ExprValueSource(fxl::RefPtr<LocalExprValue> local_source)
    : type_(Type::kLocal), local_value_(std::move(local_source)) {}

ExprValueSource::ExprValueSource(const ExprValueSource& other) = default;
ExprValueSource::ExprValueSource(ExprValueSource&& other) = default;

ExprValueSource::~ExprValueSource() {}

ExprValueSource& ExprValueSource::operator=(const ExprValueSource& other) = default;
ExprValueSource& ExprValueSource::operator=(ExprValueSource&& other) = default;

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
    case Type::kLocal:
      return "local";
  }
  return "unknown";
}

uint128_t ExprValueSource::SetBits(uint128_t existing, uint128_t new_value) const {
  FX_DCHECK(is_bitfield());

  uint128_t value = new_value << bit_shift();

  // The mask to write the value at this location, taking into account both the bit size and bit
  // shift. The bits to write to will be set to 1.
  uint128_t write_mask = (static_cast<uint128_t>(1) << bit_size()) - 1;
  write_mask <<= bit_shift();

  existing &= ~write_mask;  // Zero the dest bits we'll write.
  return existing | (value & write_mask);
}

}  // namespace zxdb
