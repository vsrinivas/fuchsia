// Copyright 2022 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include "lib/boot-shim/efi.h"

#include <efi/system-table.h>

namespace boot_shim {

void EfiSystemTableItem::Init(const efi_system_table* systab) {
  set_payload(reinterpret_cast<uintptr_t>(systab));
}

}  // namespace boot_shim
