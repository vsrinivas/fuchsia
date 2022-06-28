// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <zircon/assert.h>

#include <dev/coresight/rom_table.h>

namespace coresight {

bool RomTable::IsTable(ComponentIdRegister::Class classid, uint16_t architect,
                       uint16_t archid) const {
  if (classid == ComponentIdRegister::Class::k0x1RomTable) {
    return true;
  } else if (classid == ComponentIdRegister::Class::kCoreSight && architect == arm::kArchitect &&
             archid == arm::archid::kRomTable) {
    return true;
  }
  return false;
}

fitx::result<std::string_view, uint32_t> RomTable::EntryIndexUpperBound(
    ComponentIdRegister::Class classid, uint8_t format) const {
  if (classid == ComponentIdRegister::Class::k0x1RomTable) {
    return fitx::ok(k0x1EntryUpperBound);
  } else if (classid == ComponentIdRegister::Class::kCoreSight) {
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
