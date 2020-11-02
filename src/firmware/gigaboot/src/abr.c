// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "abr.h"

#include <lib/cksum.h>
#include <stdio.h>
#include <string.h>
#include <xefi.h>
#include <zircon/hw/gpt.h>

#include "cmdline.h"
#include "diskio.h"

#define ABR_PARTITION_NAME "misc"
#define ABR_OFFSET 0
#define CMDLINE_SLOTINFO_SIZE 32

/* === ABR sysdeps and Ops === */

uint32_t AbrCrc32(const void* buf, size_t buf_size) { return crc32(0, buf, buf_size); }

static bool read_abr_metadata(void* context, size_t size, uint8_t* buffer) {
  uint8_t guid_value[GPT_GUID_LEN] = GUID_ABR_META_VALUE;
  if (read_partition(gImg, gSys, guid_value, GUID_ABR_META_NAME, ABR_OFFSET, buffer, size) != 0) {
    printf("failed to read A/B/R metadata.\n");
    return false;
  }
  return true;
}

static bool write_abr_metadata(void* context, const uint8_t* buffer, size_t size) {
  uint8_t guid_value[GPT_GUID_LEN] = GUID_ABR_META_VALUE;
  if (write_partition(gImg, gSys, guid_value, GUID_ABR_META_NAME, ABR_OFFSET, buffer, size) != 0) {
    printf("failed to write A/B/R metadata.\n");
    return false;
  }
  return true;
}

AbrSlotIndex zircon_abr_get_boot_slot(void) {
  AbrOps ops = {.read_abr_metadata = read_abr_metadata, .write_abr_metadata = write_abr_metadata};
  return AbrGetBootSlot(&ops, false, NULL);
}

void zircon_abr_update_boot_slot_metadata(void) {
  AbrOps ops = {.read_abr_metadata = read_abr_metadata, .write_abr_metadata = write_abr_metadata};

  // Write ABR metadata updates
  AbrSlotIndex slot = AbrGetBootSlot(&ops, true, NULL);

  // TODO(puneetha) : Move this logic to verified boot
  // Write slot info to cmdline
  char slot_info[CMDLINE_SLOTINFO_SIZE] = {};
  snprintf(slot_info, sizeof(slot_info), "zvb.current_slot=%s", AbrGetSlotSuffix(slot));
  cmdline_append(slot_info, strlen(slot_info));
}

AbrResult zircon_abr_set_slot_active(int slot_number) {
  AbrOps ops = {.read_abr_metadata = read_abr_metadata, .write_abr_metadata = write_abr_metadata};

  AbrResult ret = AbrMarkSlotActive(&ops, slot_number);
  if (ret != kAbrResultOk) {
    printf("Fail to get slot info\n");
    return ret;
  }

  return 0;
}

AbrResult zircon_abr_get_slot_info(int slot_number, AbrSlotInfo* info) {
  AbrOps ops = {.read_abr_metadata = read_abr_metadata, .write_abr_metadata = write_abr_metadata};
  return AbrGetSlotInfo(&ops, slot_number, info);
}
