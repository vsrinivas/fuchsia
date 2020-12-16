// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifdef ZIRCON_BOOT_CUSTOM_SYSDEPS_HEADER
#include <zircon_boot_sysdeps.h>
#else
#include <string.h>
#endif

#include <lib/zircon_boot/zbi_utils.h>
#include <lib/zircon_boot/zircon_boot.h>

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

zbi_result_t AppendZbiFile(zbi_header_t* zbi, size_t capacity, const char* name,
                           const void* file_data, size_t file_data_size) {
  size_t name_len = strlen(name);
  if (name_len > 0xFFU) {
    zircon_boot_dlog("ZBI filename too long");
    return ZBI_RESULT_ERROR;
  }

  size_t payload_length = 1 + name_len + file_data_size;
  if (payload_length < file_data_size) {
    zircon_boot_dlog("ZBI file data too large");
    return ZBI_RESULT_TOO_BIG;
  }

  uint8_t* payload = NULL;
  zbi_result_t result = zbi_create_entry(zbi, capacity, ZBI_TYPE_BOOTLOADER_FILE, 0, 0,
                                         payload_length, (void**)&payload);
  if (result != ZBI_RESULT_OK) {
    zircon_boot_dlog("Failed to create ZBI file entry: %d\n", result);
    return result;
  }
  payload[0] = (uint8_t)name_len;
  memcpy(&payload[1], name, name_len);
  memcpy(&payload[1 + name_len], file_data, file_data_size);

  return ZBI_RESULT_OK;
}
