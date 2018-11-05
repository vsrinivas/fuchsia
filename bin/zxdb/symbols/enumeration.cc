// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/symbols/enumeration.h"

#include "lib/fxl/strings/string_printf.h"

namespace zxdb {

Enumeration::Enumeration(const std::string& name, LazySymbol type,
                         uint32_t byte_size, bool is_signed, Map map)
    : Type(Symbol::kTagEnumerationType),
      underlying_type_(std::move(type)),
      is_signed_(is_signed),
      values_(std::move(map)) {
  set_assigned_name(name);
  set_byte_size(byte_size);
}

Enumeration::~Enumeration() {}

const Enumeration* Enumeration::AsEnumeration() const { return this; }

}  // namespace zxdb
