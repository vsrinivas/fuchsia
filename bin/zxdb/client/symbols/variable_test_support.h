// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdint.h>

#include <vector>

#include "garnet/bin/zxdb/client/symbols/variable.h"

namespace zxdb {

// Returns a Variable for the given range with the given location description.
// It will be marked as having an unsigned 64-bit int type. Example:
//
//    #include "llvm/BinaryFormat/Dwarf.h"
//
//    auto var = MakeVariableForTest(
//        "var", 0x1000, 0x2000, { llvm::dwarf::DW_OP_reg0 });
fxl::RefPtr<Variable> MakeUint64VariableForTest(
    const std::string& name, uint64_t begin_ip_range, uint64_t end_ip_range,
    std::vector<uint8_t> location_expression);

}  // namespace zxdb
