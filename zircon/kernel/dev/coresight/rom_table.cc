// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <zircon/assert.h>

#include <dev/coresight/rom_table.h>

namespace coresight {

bool ROMTable::IsTable(ComponentIDRegister::Class classid, uint16_t architect,
                       uint16_t archid) const {
  if (classid == ComponentIDRegister::Class::k0x1ROMTable) {
    return true;
  } else if (classid == ComponentIDRegister::Class::kCoreSight && architect == arm::kArchitect &&
             archid == arm::archid::kROMTable) {
    return true;
  }
  return false;
}

bool ROMTable::IsTerminalEntry(uint32_t offset, ComponentIDRegister::Class classid,
                               uint16_t partid) {
  if (offset == 0) {
    return false;
  } else if (classid == ComponentIDRegister::Class::kCoreSight) {
    return true;
  }
  return classid == ComponentIDRegister::Class::kNonStandard &&
         partid == arm::partid::kTimestampGenerator;
}

fitx::result<std::string_view, uint32_t> ROMTable::EntryIndexUpperBound(
    ComponentIDRegister::Class classid, uint8_t format) const {
  if (classid == ComponentIDRegister::Class::k0x1ROMTable) {
    return fitx::ok(k0x1EntryUpperBound);
  } else if (classid == ComponentIDRegister::Class::kCoreSight) {
    switch (format) {
      case 0x0:
        return fitx::ok(k0x9NarrowEntryUpperBound);
      case 0x1:
        return fitx::ok(k0x9WideEntryUpperBound);
      default:
        printf("bad format value: %#x", format);
        return fitx::error("bad format value");
    }
  }
  return fitx::error("not a ROM table");
}

}  // namespace coresight
