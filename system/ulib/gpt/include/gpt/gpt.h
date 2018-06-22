// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <unistd.h>

#include <zircon/compiler.h>
#include <zircon/device/block.h>
#include <zircon/hw/gpt.h>

typedef gpt_entry_t gpt_partition_t;

__BEGIN_CDECLS

#define PARTITIONS_COUNT 128
#define GPT_GUID_STRLEN 37

// Helpers for translating the |name| field of "gpt_partition_t".
// Assumes UTF-16LE.
// Assumes all code points are less than or equal to U+007F, and
// discards any upper bits, forcing all inputs to be in this
// range.
//
// |len| refers to the length of the input string, in chars.
void cstring_to_utf16(uint16_t* dst, const char* src, size_t len);
// |len| refers to the length of the input string, in 16-bit pairs.
char* utf16_to_cstring(char* dst, const uint16_t* src, size_t len);

typedef struct gpt_device {
    // true if the partition table on the device is valid
    bool valid;

    // pointer to a list of partitions
    gpt_partition_t* partitions[PARTITIONS_COUNT];
} gpt_device_t;

// determines whether guid is system guid
bool gpt_is_sys_guid(uint8_t* guid, ssize_t len);

// determines whether guid is data guid
bool gpt_is_data_guid(uint8_t* guid, ssize_t len);

// determines whether guid is install guid
bool gpt_is_install_guid(uint8_t* guid, ssize_t len);

// determines whether guid is efi guid
bool gpt_is_efi_guid(uint8_t* guid, ssize_t len);

// read the partition table from the device.
int gpt_device_init(int fd, uint64_t blocksize, uint64_t blocks, gpt_device_t** out_dev);

// releases the device
void gpt_device_release(gpt_device_t* dev);

// Returns the range of usable blocks within the GPT, from [block_start, block_end] (inclusive)
int gpt_device_range(gpt_device_t* dev, uint64_t* block_start, uint64_t* block_end);

// writes the partition table to the device. it is the caller's responsibility to
// rescan partitions for the block device if needed
int gpt_device_sync(gpt_device_t* dev);

// perform all checks and computations on the in-memory representation, but DOES
// NOT write it out to disk. To perform checks AND write to disk, use
// gpt_device_sync
int gpt_device_finalize(gpt_device_t* dev);

// adds a partition
int gpt_partition_add(gpt_device_t* dev, const char* name, uint8_t* type, uint8_t* guid,
                      uint64_t offset, uint64_t blocks, uint64_t flags);

// Writes zeroed blocks at an arbitrary offset (in blocks) within the device.
//
// Can be used alongside gpt_partition_add to ensure a newly created partition
// will not read stale superblock data.
int gpt_partition_clear(gpt_device_t* dev, uint64_t offset, uint64_t blocks);

// removes a partition
int gpt_partition_remove(gpt_device_t* dev, const uint8_t* guid);

// removes all partitions
int gpt_partition_remove_all(gpt_device_t* dev);

// converts GUID to a string
void uint8_to_guid_string(char* dst, const uint8_t* src);

// given a gpt device, get the GUID for the disk
void gpt_device_get_header_guid(gpt_device_t* dev,
                                uint8_t (*disk_guid_out)[GPT_GUID_LEN]);

// return true if partition# idx has been locally modified
int gpt_get_diffs(gpt_device_t* dev, int idx, unsigned* diffs);

// print out the GPT
void print_table(gpt_device_t* device);

// Sort an array of gpt_partition_t pointers in-place based on the values of
// gpt_partition_t->first.
void gpt_sort_partitions(gpt_partition_t** partitions, size_t count);

// Attempt to read a GPT from the file descriptor. dev_out will be NULL if
// the read fails or read succeeds and GPT is invalid.
int gpt_device_read_gpt(int fd, gpt_device_t** dev_out);

void gpt_set_debug_output_enabled(bool enabled);

#define GPT_DIFF_TYPE    (0x01u)
#define GPT_DIFF_GUID    (0x02u)
#define GPT_DIFF_FIRST   (0x04u)
#define GPT_DIFF_LAST    (0x08u)
#define GPT_DIFF_FLAGS   (0x10u)
#define GPT_DIFF_NAME    (0x20u)

__END_CDECLS
