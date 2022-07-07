// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <zircon/assert.h>

#include <dev/coresight/rom_table.h>

namespace coresight {

bool RomTable::IsTable(ClassId classid, uint16_t architect, uint16_t archid) {
  if (classid == ClassId::k0x1RomTable) {
    return true;
  }
  if (classid == ClassId::kCoreSight && architect == arm::kArchitect &&
      archid == arm::archid::kRomTable) {
    return true;
  }
  return false;
}

fitx::result<std::string_view, uint32_t> RomTable::EntryIndexUpperBound(ClassId classid,
                                                                        uint8_t format) {
  if (classid == ClassId::k0x1RomTable) {
    return fitx::ok(k0x1EntryUpperBound);
  }

  // At this point we should know that this is a ROM table of CoreSight class.
  ZX_DEBUG_ASSERT(classid == ClassId::kCoreSight);
  switch (format) {
    case 0x0:
      return fitx::ok(k0x9NarrowEntryUpperBound);
    case 0x1:
      return fitx::ok(k0x9WideEntryUpperBound);
    default:
      return fitx::error("bad format value");
  }
}

}  // namespace coresight
