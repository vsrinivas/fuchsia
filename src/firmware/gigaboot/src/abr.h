// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_BOOTLOADER_SRC_ABR_H_
#define ZIRCON_BOOTLOADER_SRC_ABR_H_

#include <lib/abr/abr.h>

// Returns the current boot slot based on the ABR data
AbrSlotIndex zircon_abr_get_boot_slot(void);

// Returns ABR slot info
AbrResult zircon_abr_get_slot_info(int slot_number, AbrSlotInfo* info);

// Forces `slot_number` to be the active slot to be booted from
AbrResult zircon_abr_set_slot_active(int slot_number);

// Update ABR data for the current boot slot. To be called when the boot slot is finalized.
void zircon_abr_update_boot_slot_metadata(void);

#endif  // ZIRCON_BOOTLOADER_SRC_ABR_H_
