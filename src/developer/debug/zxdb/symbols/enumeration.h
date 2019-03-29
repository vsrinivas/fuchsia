// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdint.h>

#include <map>
#include <string>

#include "src/developer/debug/zxdb/symbols/type.h"

namespace zxdb {

// The "value" types of the enum are stored in uint64_t
class Enumeration final : public Type {
 public:
  // Construct with fxl::MakeRefCounted().

  // Maps values to enum names. The values can be either signed or unsigned.
  // In this map, everything is casted to an unsigned 64-bit value.
  using Map = std::map<uint64_t, std::string>;

  // Symbol overrides.
  const Enumeration* AsEnumeration() const;

  // Underlying type of the data. This is marked as optional in the spec
  // in which case you need to use the byte_size() and assume an integer of
  // sign matching is_signed().
  const LazySymbol& underlying_type() const { return underlying_type_; }

  // Returns true if the enum values are signed. In this case they should be
  // casted when looking up in the map (which is always unsigned).
  // Theoretically this should match underlying_type()'s signedness but there
  // may be no underlying type.
  bool is_signed() const { return is_signed_; }

  const Map& values() const { return values_; }

 private:
  FRIEND_REF_COUNTED_THREAD_SAFE(Enumeration);
  FRIEND_MAKE_REF_COUNTED(Enumeration);

  // The name can be empty for anonymous enums. The type can be empty for
  // untyped enums. The byte size must always be nonzero.
  Enumeration(const std::string& name, LazySymbol type, uint32_t byte_size,
              bool is_signed, Map map);
  virtual ~Enumeration();

  LazySymbol underlying_type_;
  uint32_t byte_size_;
  bool is_signed_;
  Map values_;
};

}  // namespace zxdb
