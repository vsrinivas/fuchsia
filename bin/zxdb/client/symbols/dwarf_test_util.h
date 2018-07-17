// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "llvm/DebugInfo/DWARF/DWARFCompileUnit.h"

namespace llvm {
class DWARFContext;
class DWARFDie;
}  // namespace llvm

namespace zxdb {

// Returns the unit in the list with a name ending in the given string.
// The name is normally the file name, so searching for "/foo.cc" will
// find the unit corresponding to foo.cc (the full path in the unit name may be
// more complicated so don't depend on the particulars of that).
llvm::DWARFUnit* GetUnitWithNameEndingIn(
    llvm::DWARFContext* context,
    llvm::DWARFUnitSection<llvm::DWARFCompileUnit>& units,
    const std::string& name);

// Returns the first DIE in the unit with the matching tag and DW_AT_Name
// attribute. If not found,t he returned DIE will be !isValid().
llvm::DWARFDie GetFirstDieOfTagAndName(
    llvm::DWARFContext* context,
    llvm::DWARFUnit* unit,
    llvm::dwarf::Tag tag,
    const std::string& name);

}  // namespace zxdb
