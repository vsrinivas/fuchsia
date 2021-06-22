// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_SYMBOLS_VARIABLE_TEST_SUPPORT_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_SYMBOLS_VARIABLE_TEST_SUPPORT_H_

#include <stdint.h>

#include <vector>

#include "src/developer/debug/zxdb/symbols/variable.h"

namespace zxdb {

// Returns a Variable for the given range with the given location description.
// Example:
//
//    #include "llvm/BinaryFormat/Dwarf.h"
//
//    auto var = MakeVariableForTest(
//        "var", my_type, 0x1000, 0x2000, { llvm::dwarf::DW_OP_reg0 });
//
// For a type that's always valid use the VariableLocation() variant:
//
//    auto var = MakeVariableForTest(
//        "var", my_type, VariableLocation(DwarfExpr({ llvm::dwarf::DW_OP_reg0 })));
//
// This variant also allows all of the more complex variants like multiple valid ranges.
fxl::RefPtr<Variable> MakeVariableForTest(const std::string& name, fxl::RefPtr<Type> type,
                                          VariableLocation loc);
fxl::RefPtr<Variable> MakeVariableForTest(const std::string& name, fxl::RefPtr<Type> type,
                                          uint64_t begin_ip_range, uint64_t end_ip_range,
                                          DwarfExpr location_expression);

// Like above but marks the variable as having an unsigned 64-bit int type.
fxl::RefPtr<Variable> MakeUint64VariableForTest(const std::string& name, VariableLocation loc);
fxl::RefPtr<Variable> MakeUint64VariableForTest(const std::string& name, uint64_t begin_ip_range,
                                                uint64_t end_ip_range,
                                                DwarfExpr location_expression);

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_SYMBOLS_VARIABLE_TEST_SUPPORT_H_
