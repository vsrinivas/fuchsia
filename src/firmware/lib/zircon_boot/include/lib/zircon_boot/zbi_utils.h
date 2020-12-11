// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_FIRMWARE_LIB_ZIRCON_BOOT_INCLUDE_LIB_ZIRCON_BOOT_ZBI_UTILS_H_
#define SRC_FIRMWARE_LIB_ZIRCON_BOOT_INCLUDE_LIB_ZIRCON_BOOT_ZBI_UTILS_H_

// This should point to the abr.h in the abr library in firmware sdk.
#include <lib/abr/abr.h>
// This should point to the zbi.h in the zbi library in firmware sdk..
#include <lib/zbi/zbi.h>

__BEGIN_CDECLS

// Appends a cmdline ZBI item containing the current slot information to a ZBI container.
// For example, "zvb.current_slot=_a"
//
// Returns ZBI_RESULT_OK on success, error code otherwise.
zbi_result_t AppendCurrentSlotZbiItem(zbi_header_t* zbi, size_t capacity, AbrSlotIndex slot);

// TODO(b/174968242): Add wrappers and APIs for special ZBI items such as bootloader
// file and bootarg items, so that the logic does not have to be re-implemented
// for every new device.

__END_CDECLS

#endif  // SRC_FIRMWARE_LIB_ZIRCON_BOOT_INCLUDE_LIB_ZIRCON_BOOT_ZBI_UTILS_H_
