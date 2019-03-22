// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <cstdint>
#include <map>

#include "garnet/bin/zxdb/common/err.h"
#include "garnet/public/lib/fxl/logging.h"
#include "src/developer/debug/ipc/protocol.h"
#include "src/developer/debug/ipc/records.h"

// Idiomatic classes that wrap over the debug_ipc definitions of this info.

namespace zxdb {

class Register;  // Defined below

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
  RegisterSet(const RegisterSet&);
  RegisterSet(RegisterSet&&);
  RegisterSet& operator=(const RegisterSet&);
  RegisterSet& operator=(RegisterSet&&);

  debug_ipc::Arch arch() const { return arch_; }
  void set_arch(debug_ipc::Arch arch) { arch_ = arch; }  // Mainly for tests.

  CategoryMap& category_map() { return category_map_; }
  virtual const CategoryMap& category_map() const { return category_map_; }

  // Shorthand for looking over the category map.
  const Register* operator[](debug_ipc::RegisterID) const;

 private:
  CategoryMap category_map_;
  debug_ipc::Arch arch_ = debug_ipc::Arch::kUnknown;
};

// Main wrapper over the register information. Also holds information about the
// sub-registers associated with a particular instance of the registers.
// TODO(donosoc): Do the sub-register mapping.
class Register {
 public:
  using const_iterator = const uint8_t*;

  explicit Register(debug_ipc::Register);

  debug_ipc::RegisterID id() const { return reg_.id; }

  size_t size() const { return reg_.data.size(); }  // In bytes.

  std::vector<uint8_t>& data() { return reg_.data; }
  const std::vector<uint8_t>& data() const { return reg_.data; }

  // Intended for <=64-bits values, check length.
  uint64_t GetValue() const;

  const_iterator begin() const { return &reg_.data[0]; }
  const_iterator end() const { return &reg_.data.back() + 1; }

 private:
  debug_ipc::Register reg_;
};

}  // namespace zxdb
