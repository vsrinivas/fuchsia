// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/zircon_boot/zbi_utils.h>
#include <lib/zircon_boot/zircon_boot.h>
#include <string.h>

#include "utils.h"

zbi_result_t AppendCurrentSlotZbiItem(zbi_header_t* image, size_t capacity, AbrSlotIndex slot) {
  char buffer[] = "zvb.current_slot=__";
  const char* suffix = AbrGetSlotSuffix(slot);
  if (strlen(suffix) != 2) {
    zircon_boot_dlog("unexpected suffix format %s\n", suffix);
    return ZBI_RESULT_ERROR;
  }
  buffer[sizeof(buffer) - 2] = suffix[1];
  return zbi_create_entry_with_payload(image, capacity, ZBI_TYPE_CMDLINE, 0, 0, buffer,
                                       sizeof(buffer));
}
