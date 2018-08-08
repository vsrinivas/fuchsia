// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <inttypes.h>

#include "garnet/bin/zxdb/client/register.h"
#include "garnet/bin/zxdb/client/register_dwarf.h"
#include "garnet/bin/zxdb/client/session.h"
#include "garnet/public/lib/fxl/logging.h"
#include "garnet/public/lib/fxl/strings/string_printf.h"

namespace zxdb {

// RegisterSet -----------------------------------------------------------------

RegisterSet::RegisterSet() = default;

RegisterSet::RegisterSet(debug_ipc::Arch arch,
                         std::vector<debug_ipc::RegisterCategory> categories)
    : arch_(arch) {
  for (auto& category : categories) {
    std::vector<Register> registers;
    registers.reserve(category.registers.size());
    for (auto& ipc_reg : category.registers) {
      registers.emplace_back(Register(std::move(ipc_reg)));
    }

    category_map_[category.type] = std::move(registers);
  }
}

RegisterSet::RegisterSet(RegisterSet&&) = default;
RegisterSet& RegisterSet::operator=(RegisterSet&&) = default;

RegisterSet::~RegisterSet() = default;

const Register* RegisterSet::operator[](debug_ipc::RegisterID id) const {
  if (id == debug_ipc::RegisterID::kUnknown)
    return nullptr;

  // If this becomes to costly, switch to a cache RegisterID <--> Register map.
  const Register* found_reg = nullptr;
  for (const auto& kv : category_map_) {
    for (const auto& reg : kv.second) {
      if (reg.id() == id) {
        found_reg = &reg;
        break;
      }
    }
  }
  return found_reg;
}

const Register* RegisterSet::GetRegisterFromDWARF(uint32_t dwarf_reg_id) const {
  debug_ipc::RegisterID reg_id = GetDWARFRegisterID(arch_, dwarf_reg_id);
  // If kUnknown, this will return null.
  return (*this)[reg_id];
}

bool RegisterSet::GetRegisterValueFromDWARF(uint32_t dwarf_reg_id,
                                            uint64_t* out) const {
  const Register* reg = GetRegisterFromDWARF(dwarf_reg_id);
  if (!reg)
    return false;
  *out = reg->GetValue();
  return true;
}

bool RegisterSet::GetRegisterDataFromDWARF(uint32_t dwarf_reg_id,
                                           std::vector<uint8_t>* data) const {
  const Register* reg = GetRegisterFromDWARF(dwarf_reg_id);
  if (!reg)
    return false;

  data->clear();
  data->reserve(reg->size());
  data->insert(data->begin(), reg->begin(), reg->end());
  return true;
}

// Register --------------------------------------------------------------------

namespace {

template <typename UintType>
inline UintType ReadRegisterData(const Register& reg) {
  FXL_DCHECK(reg.size() == sizeof(UintType));
  return *reinterpret_cast<const UintType*>(reg.begin());
}

}  // namespace

Register::Register(debug_ipc::Register reg) : reg_(std::move(reg)) {}

uint64_t Register::GetValue() const {
  switch (size()) {
    case 1:
      return ReadRegisterData<uint8_t>(*this);
    case 2:
      return ReadRegisterData<uint16_t>(*this);
    case 4:
      return ReadRegisterData<uint32_t>(*this);
    case 8:
      return ReadRegisterData<uint64_t>(*this);
    default:
      FXL_NOTREACHED() << fxl::StringPrintf("Invalid size for %s: %lu",
                                            __PRETTY_FUNCTION__, size());
      return 0;
  }
}

}  // namespace zxdb
