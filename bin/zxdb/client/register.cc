// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/client/register.h"
#include "garnet/bin/zxdb/client/session.h"
#include "garnet/bin/zxdb/client/thread.h"
#include "garnet/public/lib/fxl/logging.h"
#include "garnet/public/lib/fxl/strings/string_printf.h"

namespace zxdb {

// RegisterSet -----------------------------------------------------------------

RegisterSet::RegisterSet() = default;

RegisterSet::RegisterSet(std::vector<debug_ipc::RegisterCategory> categories) {
  for (auto& category : categories) {
    std::vector<Register> registers;
    registers.reserve(category.registers.size());
    for (auto& ipc_reg : category.registers) {
      registers.emplace_back(Register(std::move(ipc_reg)));
    }

    register_map_[category.type] = std::move(registers);
  }
}

RegisterSet::RegisterSet(RegisterSet&&) = default;
RegisterSet& RegisterSet::operator=(RegisterSet&&) = default;

RegisterSet::~RegisterSet() = default;

// Register --------------------------------------------------------------------

namespace {

template <typename UintType>
inline UintType ReadRegisterData(const Register& reg) {
  FXL_DCHECK(reg.size() == sizeof(UintType));
  return *reinterpret_cast<const UintType*>(reg.begin());
}

}   // namespace

Register::Register(debug_ipc::Register reg)
    : reg_(std::move(reg)) {}

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

}   // namespace zxdb
