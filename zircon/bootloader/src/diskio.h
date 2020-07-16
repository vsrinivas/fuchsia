// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_BOOTLOADER_SRC_DISKIO_H_
#define ZIRCON_BOOTLOADER_SRC_DISKIO_H_

#include <efi/system-table.h>

efi_status read_partition(efi_handle img, efi_system_table* sys, const uint8_t* guid_value,
                          const char* guid_name, uint64_t offset, unsigned char* data, size_t size);

efi_status write_partition(efi_handle img, efi_system_table* sys, const uint8_t* guid_value,
                           const char* guid_name, uint64_t offset, const unsigned char* data,
                           size_t size);

void* image_load_from_disk(efi_handle img, efi_system_table* sys, size_t* sz,
                           const uint8_t* guid_value, const char* guid_name);

#endif  // ZIRCON_BOOTLOADER_SRC_DISKIO_H_
