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

uint32_t ROMTable::EntryIndexUpperBound(ComponentIDRegister::Class classid, uint8_t format) const {
  if (classid == ComponentIDRegister::Class::k0x1ROMTable) {
    return k0x1EntryUpperBound;
  } else if (classid == ComponentIDRegister::Class::kCoreSight) {
    switch (format) {
      case 0x0:
        return k0x9NarrowEntryUpperBound;
      case 0x1:
        return k0x9WideEntryUpperBound;
      default:
        ZX_ASSERT_MSG(false, "unknown DEVID.FORMAT value: %#x", format);
    }
  }
  ZX_ASSERT_MSG(false, "a ROM table cannot have a class of %#hhx (%s)", classid,
                ToString(classid).data());
}

}  // namespace coresight
