// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_FIRMWARE_GIGABOOT_SRC_DISKIO_H_
#define SRC_FIRMWARE_GIGABOOT_SRC_DISKIO_H_

#include <zircon/compiler.h>
#include <zircon/hw/gpt.h>

#include <efi/protocol/disk-io.h>
#include <efi/system-table.h>

__BEGIN_CDECLS

// Max number of UTF-16 chars in a GPT partition name.
#define GPT_NAME_LEN_U16 (GPT_NAME_LEN / sizeof(uint16_t))

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

// Load a ZBI from the disk that contains the bootloader.
//
// img: efi_handle passed to efi_main
// sys: efi system table
// extra_space: extra padding to add to the end of the ZBI, e.g. for appending ZBI items.
// sz: set to loaded size of the ZBI + extra_space (i.e. the maximum size available for the ZBI).
// guid_value: GUID of partition to load from.
// guid_name: user-friendly name for guid_value.
//
// Returns a pointer to the loaded ZBI, or NULL if no ZBI was loaded.
void* image_load_from_disk(efi_handle img, efi_system_table* sys, size_t extra_space, size_t* sz,
                           const uint8_t* guid_value, const char* guid_name);

// Returns true if the disk device that was used to load the bootloader
// is connected via USB.
bool is_booting_from_usb(efi_handle img, efi_system_table* sys);

// Find the disk device that was used to load the boot loader.
// Returns 0 on success and fills in the disk pointer, -1 otherwise.
int disk_find_boot(efi_handle img, efi_system_table* sys, bool verbose, disk_t* disk);

// Reads the GPT from the front of |disk| and finds the requested partition.
//
// The matcher will find a partition which satisfies all of the given |type|,
// |guid|, and |name| parameters.
//
// disk: the disk to search.
// verbose: true to print additional debug info.
// type: partition type GUID, or NULL to match any.
// guid: partition GUID, or NULL to match any.
// name: UTF-8 partition name, or NULL to match any.
// partition: on success, filled with the resulting GPT partition entry. Note
//            that .first and .last are in block units, and .name is UTF-16.
//
// Returns 0 on success, -1 if no partitions or multiple partitions match.
int disk_find_partition(const disk_t* disk, bool verbose, const uint8_t* type, const uint8_t* guid,
                        const char* name, gpt_entry_t* partition);

// Matcher callback for disk_find_partition_custom.
// Returns true to continue iterating, false to stop iteration.
typedef bool (*partition_matcher_cb)(void* ctx, const gpt_entry_t* partition);

// Reads the GPT from the front of |disk| and calls a callback for each partition.
//
// disk: the disk to search.
// verbose: true to print additional debug info.
// matcher: callback that is called with each partition's gpt_entry_t.
// matcher_ctx: extra context passed to |matcher|.
//
// Returns 0 if scan succeeded, -1 if scan failed (e.g. invalid GPT).
int disk_scan_partitions(const disk_t* disk, bool verbose, partition_matcher_cb matcher,
                         void* matcher_ctx);

efi_status disk_write(disk_t* disk, size_t offset, void* data, size_t length);

// Reads data from |disk|.
//
// disk: disk to read from.
// offset: start position, in bytes.
// data: pointer to buffer to read into.
// length: amount of data to read, in bytes (must be <= size of the `data` buffer).
efi_status disk_read(const disk_t* disk, size_t offset, void* data, size_t length);

// Converts a user-facing partition name into a type GUID.
//
// Accepts both legacy and new partition names, but always returns the legacy
// type GUID since that's what all Gigaboot devices use at the moment. Accepting
// both names will allow us to start moving over to the new partition scheme in
// the future if we want.
//
// name: partition name.
//
// Returns the matching partition type GUID, or NULL if no match was found.
const uint8_t* partition_type_guid(const char* name);

__END_CDECLS

#endif  // SRC_FIRMWARE_GIGABOOT_SRC_DISKIO_H_
