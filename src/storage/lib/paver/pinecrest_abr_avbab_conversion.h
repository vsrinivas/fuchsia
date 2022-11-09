/* Copyright 2022 The Fuchsia Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef SRC_STORAGE_LIB_PAVER_PINECREST_ABR_AVBAB_CONVERSION_H_
#define SRC_STORAGE_LIB_PAVER_PINECREST_ABR_AVBAB_CONVERSION_H_

#include <lib/abr/abr.h>

#ifdef __cplusplus
extern "C" {
#endif

// For devices that only uses avb ab. We need to make sure Fuchsia libabr is
// compatible with it. There are two major differences between libabr and avb
// ab:
//   1. Some implementation (i.e. Pinecrest) avb ab uses little endian for crc
//      when writing to storage.
//   2. Avb ab doesn't have the concept of recovery. When both slot are marked
//      unbootable, avb ab will hangs. libabr will attempt boot R slot.
//
// Here's our approach. For 1, we need to adjust crc into little endian before
// writing to storage. For 2, before we write to storage, check if both slot is
// marked unbootable. If they are, mark the current bootloader slot as
// successful so that this bootloader can still boot. But also set a flag
// in the AbrData.reserve2 field to indicate that both kernel slots are
// unbootable. When metadata is read from storage and before presentting to
// libabr, check if the reserved flag is set. If set, marked both slot
// unbootable.
//
// The code is written in C because it needs to be shared with bootloader.

// Convert an avbab metadata read from storage to a libabr metadata to present
// to libabr. It adjust crc endian order and mark both slot unbootable if the
// reserved flag is set.
void avbab_to_abr(AbrData *data);

// Convert an libabr metadata pass from libabr to avb ab metadata to be written
// to storage. It adjusts crc endian order and if both slots are marked
// unbootable, restores the tries_remaining of `current_firmware_slot` and set
// the reserved flag.
bool abr_to_avbab(AbrData *data, AbrSlotIndex current_firmware_slot);

// APIs to clear/set/get the reserved flag.
void abr_clear_reserve2_ab_slot_unbootable(AbrData *data);
void abr_set_reserve2_ab_slot_unbootable(AbrData *data);
uint8_t abr_get_reserve2_ab_slot_unbootable(AbrData *data);

#ifdef __cplusplus
}
#endif

#endif  // SRC_STORAGE_LIB_PAVER_PINECREST_ABR_AVBAB_CONVERSION_H_
