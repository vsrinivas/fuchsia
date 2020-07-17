// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_BOOTLOADER_SRC_DISKIO_H_
#define ZIRCON_BOOTLOADER_SRC_DISKIO_H_

#include <efi/protocol/disk-io.h>
#include <efi/system-table.h>

typedef struct {
  efi_disk_io_protocol* io;
  efi_handle h;
  efi_boot_services* bs;
  efi_handle img;
  uint64_t first;
  uint64_t last;
  uint32_t blksz;
  uint32_t id;
} disk_t;

efi_status read_partition(efi_handle img, efi_system_table* sys, const uint8_t* guid_value,
                          const char* guid_name, uint64_t offset, unsigned char* data, size_t size);

efi_status write_partition(efi_handle img, efi_system_table* sys, const uint8_t* guid_value,
                           const char* guid_name, uint64_t offset, const unsigned char* data,
                           size_t size);

void* image_load_from_disk(efi_handle img, efi_system_table* sys, size_t* sz,
                           const uint8_t* guid_value, const char* guid_name);

// Find the disk device that was used to load the boot loader.
// Returns 0 on success and fills in the disk pointer, -1 otherwise.
int disk_find_boot(efi_handle img, efi_system_table* sys, bool verbose, disk_t* disk);

// Given a disk structure, find the kernel on that disk by reading the partition table
// and looking for the partition with the supplied guid_name.
int disk_find_partition(disk_t* disk, bool verbose, const uint8_t* guid_value,
                        const char* guid_name);

efi_status disk_write(disk_t* disk, size_t offset, void* data, size_t length);

// guid_value_from_name takes in a GUID name and puts the associated GUID value
// into value.
// Returns 0 on success, -1 if the guid_name was not found.
int guid_value_from_name(char *guid_name, uint8_t *value);


#endif  // ZIRCON_BOOTLOADER_SRC_DISKIO_H_
