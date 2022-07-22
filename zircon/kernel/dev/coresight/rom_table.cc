// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

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

}  // namespace coresight
