// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/client/symbols/dwarf_test_util.h"

#include "garnet/bin/zxdb/client/string_util.h"
#include "garnet/bin/zxdb/client/symbols/dwarf_die_decoder.h"

namespace zxdb {

namespace {

// Returns the DW_AT_Name for the given DIE or the empty string on failure.
std::string GetDIEName(llvm::DWARFContext* context, llvm::DWARFUnit* unit,
                       const llvm::DWARFDie& die) {
  DwarfDieDecoder decoder(context, unit);

  llvm::Optional<const char*> name;
  decoder.AddCString(llvm::dwarf::DW_AT_name, &name);

  if (!decoder.Decode(die) || !name)
    return std::string();
  return std::string(*name);
}

}  // namespace

llvm::DWARFUnit* GetUnitWithNameEndingIn(
    llvm::DWARFContext* context,
    llvm::DWARFUnitSection<llvm::DWARFCompileUnit>& units,
    const std::string& name) {
  for (unsigned i = 0; i < units.size(); i++) {
    llvm::DWARFCompileUnit* unit = units[i].get();
    std::string unit_name = GetDIEName(context, unit, unit->getUnitDIE());
    if (StringEndsWith(unit_name, name))
      return unit;
  }
  return nullptr;
}

llvm::DWARFDie GetFirstDieOfTagAndName(llvm::DWARFContext* context,
                                       llvm::DWARFUnit* unit,
                                       llvm::dwarf::Tag tag,
                                       const std::string& name) {
  for (unsigned i = 0; i < unit->getNumDIEs(); i++) {
    llvm::DWARFDie die = unit->getDIEAtIndex(i);
    if (die.getTag() == tag) {
      if (GetDIEName(context, unit, die) == name)
        return die;
    }
  }
  return llvm::DWARFDie();
}

}  // namespace zxdb
