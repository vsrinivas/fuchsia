// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <cstdint>
#include <map>

#include "garnet/lib/debug_ipc/protocol.h"
#include "garnet/lib/debug_ipc/records.h"
#include "garnet/public/lib/fxl/logging.h"

// Idiomatic classes that wrap over the debug_ipc definitions of this info.
// TODO(donosoc): Match this information to the DWARF equivalents.

namespace zxdb {

class Register;     // Defined below

// All the information of the registers for a particular thread.
// TODO(donosoc): Do the DWARF<->zxdb register mapping
class RegisterSet {
 public:
  using RegisterMap = std::map<debug_ipc::RegisterCategory::Type,
                               std::vector<Register>>;

  RegisterSet();
  RegisterSet(std::vector<debug_ipc::RegisterCategory>);
  virtual ~RegisterSet();

  // Movable
  RegisterSet(RegisterSet&&);
  RegisterSet& operator=(RegisterSet&&);

  virtual const RegisterMap& register_map() const { return register_map_; }

 private:
  RegisterMap register_map_;

  FXL_DISALLOW_COPY_AND_ASSIGN(RegisterSet);
};

// Main wrapper over the register information. Also holds information about the
// sub-registers associated with a particular instance of the registers.
// TODO(donosoc): Do the sub-register mapping.
class Register {
 public:
  using const_iterator = const uint8_t*;

  explicit Register(debug_ipc::Register);

  debug_ipc::RegisterID id() const { return reg_.id; }

  size_t size() const { return reg_.data.size(); }   // In bytes

  // Intented for <=64-bits values, check length
  uint64_t GetValue() const;

  const_iterator begin() const { return &reg_.data[0]; }
  const_iterator end() const { return &reg_.data.back() + 1; }

 private:
  debug_ipc::Register reg_;
};

}   // namespace zxdb
