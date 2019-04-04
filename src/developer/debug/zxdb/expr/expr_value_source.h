// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdint.h>

namespace zxdb {

// Holds the source of a value. This allows taking the address of an object
// stored in an ExprValue ("&foo"), and for updating the contents of variables
// (currently not supported yet).
class ExprValueSource {
 public:
  // Where this value came from.
  //
  // TODO(brettw) We will want to add a "register" mode along with some
  // indication of what register it was.
  enum class Type { kTemporary, kMemory };

  // Indicates an unknown or temporary source (say, the output of "i + 4").
  ExprValueSource() = default;

  // Initializes the source indicating a memory address.
  explicit ExprValueSource(uint64_t address)
      : type_(Type::kMemory), address_(address) {}

  Type type() const { return type_; }

  // Valid when type_ == kAddress.
  uint64_t address() const { return address_; }

  // Returns a new ExprValueSource pointing to the given offset inside of this
  // one. If this one is not in memory, the returned one will be the same.
  ExprValueSource GetOffsetInto(uint32_t offset) const {
    if (type_ == Type::kMemory)
      return ExprValueSource(address_ + offset);
    return ExprValueSource();
  }

  bool operator==(const ExprValueSource& other) const {
    return type_ == other.type_ && address_ == other.address_;
  }
  bool operator!=(const ExprValueSource& other) const {
    return !operator==(other);
  }

 private:
  Type type_ = Type::kTemporary;
  uint64_t address_ = 0;
};

}  // namespace zxdb
