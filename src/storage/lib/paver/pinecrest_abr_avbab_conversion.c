/* Copyright 2022 The Fuchsia Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "pinecrest_abr_avbab_conversion.h"

#include <lib/abr/abr.h>
#include <lib/abr/util.h>

// Offset in AbrData.reserved2 to use as ab kernel slot unbootable flag
// TODO(b/255567130): Double check and pick one that is not used.
#define RESERVE2_AB_SLOT_UNBOOTABLE_INDEX 0
_Static_assert(offsetof(AbrData, reserved2[RESERVE2_AB_SLOT_UNBOOTABLE_INDEX]) == 17,
               "AbrData layout change. Please revisit this choice");

uint8_t abr_get_reserve2_ab_slot_unbootable(AbrData *data) {
  return data->reserved2[RESERVE2_AB_SLOT_UNBOOTABLE_INDEX];
}

void abr_set_reserve2_ab_slot_unbootable(AbrData *data) {
  data->reserved2[RESERVE2_AB_SLOT_UNBOOTABLE_INDEX] = 1;
}

void abr_clear_reserve2_ab_slot_unbootable(AbrData *data) {
  data->reserved2[RESERVE2_AB_SLOT_UNBOOTABLE_INDEX] = 0;
}

// Convert a libabr metadata to avb ab metadata
bool abr_to_avbab(AbrData *data, AbrSlotIndex current_firmware_slot) {
  // Convert only if it is a valid abr data
  uint32_t crc_value = AbrHostToBigEndian(AbrCrc32(data, sizeof(*data) - sizeof(uint32_t)));
  if (crc_value != data->crc32) {
    return true;
  }

  // If both slot unbootable, we need to pick a bootloader slot to boot as R next time.
  if (data->slot_data[0].tries_remaining == 0 && data->slot_data[0].successful_boot == 0 &&
      data->slot_data[1].tries_remaining == 0 && data->slot_data[1].successful_boot == 0) {
    // Use the current bootloader slot as R, since we know we are running fuchsia.
    if (current_firmware_slot != kAbrSlotIndexA && current_firmware_slot != kAbrSlotIndexB) {
      return false;
    }
    data->slot_data[current_firmware_slot].successful_boot = 1;
    // Set a special flag to indicate that kernel slots are unbootable.
    abr_set_reserve2_ab_slot_unbootable(data);
  }

  // Use little endian crc
  data->crc32 = AbrCrc32(data, sizeof(*data) - sizeof(uint32_t));
  return true;
}

void avbab_to_abr(AbrData *data) {
  // Check if metadata is valid. If valid, do avb ab to abr conversion.
  // Otherwise simply returns.
  uint32_t crc_value = AbrCrc32(data, sizeof(*data) - sizeof(uint32_t));
  if (crc_value != data->crc32) {
    return;
  }

  // If both slot are unbootable according to the reserved fields, adjust
  // slot data.
  if (abr_get_reserve2_ab_slot_unbootable(data)) {
    data->slot_data[0].tries_remaining = 0;
    data->slot_data[0].successful_boot = 0;
    data->slot_data[1].tries_remaining = 0;
    data->slot_data[1].successful_boot = 0;
    // Clear the flag, so that it doesn't persist to the next write.
    abr_clear_reserve2_ab_slot_unbootable(data);
  }

  // Use big endian crc
  data->crc32 = AbrHostToBigEndian(AbrCrc32(data, sizeof(*data) - sizeof(uint32_t)));
}
