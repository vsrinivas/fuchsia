// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <cstdint>
#include <map>

#include "garnet/bin/zxdb/client/err.h"
#include "garnet/lib/debug_ipc/protocol.h"
#include "garnet/lib/debug_ipc/records.h"
#include "garnet/public/lib/fxl/logging.h"

// Idiomatic classes that wrap over the debug_ipc definitions of this info.

namespace zxdb {

class Register;     // Defined below

// All the information of the registers for a particular thread.
class RegisterSet {
 public:
  // Currently accessing a register is iterating over the categories.
  // If this gets slow, a map from ID -> Register might be needed.
  using CategoryMap =
      std::map<debug_ipc::RegisterCategory::Type, std::vector<Register>>;

  RegisterSet();
  RegisterSet(debug_ipc::Arch, std::vector<debug_ipc::RegisterCategory>);
  virtual ~RegisterSet();

  // Movable
  RegisterSet(RegisterSet&&);
  RegisterSet& operator=(RegisterSet&&);

  debug_ipc::Arch arch() const { return arch_; }
  void set_arch(debug_ipc::Arch arch) { arch_ = arch; }   // Mainly for tests.

  virtual const CategoryMap& category_map() const { return category_map_; }

  // Shorthand for looking over the category map.
  const Register* operator[](debug_ipc::RegisterID) const;

  // DWARF mapping -------------------------------------------------------------

  // If the provided DWARF reg id doesn't match the architecture or is an
  // unknown value, this will return null.
  const Register* GetRegisterFromDWARF(uint32_t dwarf_reg_id) const;

  // Value shorthands. Uses GetDWARFRegister.
  bool GetRegisterValueFromDWARF(uint32_t dwarf_reg_id, uint64_t* out) const;
  // For >64 bit long registers.
  bool GetRegisterDataFromDWARF(uint32_t dwarf_reg_id,
                                std::vector<uint8_t>* out) const;

 private:
  CategoryMap category_map_;
  debug_ipc::Arch arch_;

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

  size_t size() const { return reg_.data.size(); }   // In bytes.

  // Intented for <=64-bits values, check length.
  uint64_t GetValue() const;

  const_iterator begin() const { return &reg_.data[0]; }
  const_iterator end() const { return &reg_.data.back() + 1; }

 private:
  debug_ipc::Register reg_;
};

}   // namespace zxdb
