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
// Returns ZBI_RESULT_OK on success, Error code otherwise.
zbi_result_t AppendCurrentSlotZbiItem(zbi_header_t* zbi, size_t capacity, AbrSlotIndex slot);

// Appends a file to a ZBI container. The function creates a new entry of ZBI_TYPE_BOOTLOADER_FILE
// type in the container, containing file name and content. The file will be available by the
// filesystem service in bootsvc.
//
// @zbi: Pointer to the container.
// @capacity: Size of the container.
// @name: Name of the file.
// @file_data: Content of the file.
// @file_data_size: Size of the content.
//
// Returns ZBI_RESULT_OK on success. Error code otherwise.
zbi_result_t AppendZbiFile(zbi_header_t* zbi, size_t capacity, const char* name,
                           const void* file_data, size_t file_data_size);
__END_CDECLS

#endif  // SRC_FIRMWARE_LIB_ZIRCON_BOOT_INCLUDE_LIB_ZIRCON_BOOT_ZBI_UTILS_H_
