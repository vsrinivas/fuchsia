// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "garnet/lib/debug_ipc/protocol.h"

namespace zxdb {

// Obtains the RegisterID for a particular DWARF register.
// To get the actual value of that register, use a RegisterSet.
debug_ipc::RegisterID GetDWARFRegisterID(debug_ipc::Arch,
                                         uint32_t dwarf_reg_id);

// Some registers have special meaning. This function returns whether and what
// speciual meaning the given DWARF register ID has on the given platform.
enum class SpecialRegisterType { kNone, kIP, kSP, kBP };
SpecialRegisterType GetSpecialRegisterTypeFromDWARFRegisterID(
    debug_ipc::Arch, uint32_t dwarf_reg_id);

// Returns the register ID for the given special register.
debug_ipc::RegisterID GetSpecialRegisterID(
    debug_ipc::Arch, SpecialRegisterType);

// Platform specific -----------------------------------------------------------

debug_ipc::RegisterID GetARMv8DWARFRegisterID(uint32_t dwarf_reg_id);
debug_ipc::RegisterID GetX64DWARFRegisterID(uint32_t dwarf_reg_id);

}  // namespace zxdb
