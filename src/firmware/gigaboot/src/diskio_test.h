// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_FIRMWARE_GIGABOOT_SRC_DISKIO_TEST_H_
#define SRC_FIRMWARE_GIGABOOT_SRC_DISKIO_TEST_H_

#include <lib/efi/testing/fake_disk_io_protocol.h>
#include <lib/efi/testing/stub_boot_services.h>
#include <zircon/hw/gpt.h>

#include <cstdint>
#include <vector>

#include <efi/boot-services.h>
#include <efi/protocol/block-io.h>
#include <efi/protocol/device-path.h>
#include <efi/protocol/disk-io.h>
#include <efi/protocol/loaded-image.h>

#include "diskio.h"

namespace gigaboot {

// Arbitrary values chosen for testing, these can be modified if needed.
// The block size just has to be 8-byte aligned for easy casting.
inline constexpr uint32_t kBootMediaId = 3;
inline constexpr uint32_t kBootMediaBlockSize = 512;
inline constexpr uint64_t kBootMediaNumBlocks = 142;
inline constexpr uint64_t kBootMediaSize = kBootMediaBlockSize * kBootMediaNumBlocks;
static_assert(kBootMediaBlockSize % 8 == 0, "Block size must be 8-byte aligned");

// These values don't matter, they're just arbitrary handles, but make them
// somewhat recognizable so that if a failure occurs it's easy to tell which
// one it's referring to.
static inline efi_handle ImageHandle() { return reinterpret_cast<efi_handle>(0x10); }
static inline efi_handle DeviceHandle() { return reinterpret_cast<efi_handle>(0x20); }
static inline efi_handle BlockHandle() { return reinterpret_cast<efi_handle>(0x30); }

// A set of partitions we can use to set up a fake GPT.
// These are broken out as individual variables as well to make it easy to
// grab GUIDs when needed.
inline constexpr gpt_entry_t kZirconAGptEntry = {
    .type = GPT_ZIRCON_ABR_TYPE_GUID,
    .guid = {0x01},
    .first = 3,
    .last = 4,
    // Partition names are little-endian UTF-16.
    .name = "z\0i\0r\0c\0o\0n\0_\0a\0",
};
inline constexpr gpt_entry_t kZirconBGptEntry = {
    .type = GPT_ZIRCON_ABR_TYPE_GUID,
    .guid = {0x02},
    .first = 5,
    .last = 6,
    .name = "z\0i\0r\0c\0o\0n\0_\0b\0",
};
inline constexpr gpt_entry_t kZirconRGptEntry = {
    .type = GPT_ZIRCON_ABR_TYPE_GUID,
    .guid = {0x03},
    .first = 7,
    .last = 8,
    .name = "z\0i\0r\0c\0o\0n\0_\0r\0",
};
inline constexpr gpt_entry_t kFvmGptEntry = {
    .type = GPT_FVM_TYPE_GUID,
    .guid = {0x04},
    .first = 9,
    .last = 11,
    .name = "f\0v\0m\0",
};
const gpt_entry_t kVbmetaAGptEntry = {
    .type = GPT_VBMETA_ABR_TYPE_GUID,
    .guid = {0x05},
    // The libavb code is hardcoded to read 64k of vbmeta data, so this partition needs to be big
    // enough.
    .first = 12,
    .last = 140,
    .name = "v\0b\0m\0e\0t\0a\0-\0a\0",
};
const gpt_entry_t kMiscGptEntry = {
    .type = GUID_ABR_META_VALUE,
    .guid = {0x06},
    .first = 141,
    .last = 141,
    .name = "m\0i\0s\0c\0",
};

// The state necessary to set up mocks for disk_find_boot().
// The default values will result in a successful execution.
struct DiskFindBootState {
  // Empty paths are the simplest way to satisfy the path matching check.
  efi_device_path_protocol device_path = {
      .Type = DEVICE_PATH_END,
      .SubType = DEVICE_PATH_END,
      .Length = {0, 0},
  };

  efi_loaded_image_protocol loaded_image = {
      .DeviceHandle = DeviceHandle(),
      .FilePath = &device_path,
  };

  // disk_find_boot() doesn't use any block I/O callbacks, just the media
  // information.
  efi_block_io_media media = {
      .MediaId = kBootMediaId,
      .MediaPresent = true,
      .LogicalPartition = false,
      .BlockSize = kBootMediaBlockSize,
      .LastBlock = kBootMediaNumBlocks - 1,
  };

  efi_block_io_protocol block_io = {
      .Media = &media,
  };
};

// Returns a disk_t with reasonable default values to represent the boot media.
disk_t TestBootDisk(efi_disk_io_protocol* disk_protocol, efi_boot_services* boot_services);

// Writes a primary GPT to |fake_disk| such that it will contain the given
// |partitions|. Partition contents on disk are unchanged.
//
// This will use blocks 0-2 for MBR/header/partition data, so a
// properly-configured set of partitions should only use blocks in the range
// [3, kBootMediaNumBlocks).
//
// Should be called with ASSERT_NO_FATAL_FAILURES().
void SetupDiskPartitions(efi::FakeDiskIoProtocol& fake_disk,
                         const std::vector<gpt_entry_t>& partitions);

// Performs all the necessary mocking so that disk_find_boot() will complete
// successfully.
//
// The returned object holds the state necessary for the mocks and must be kept
// in scope until disk_find_boot() is called, after which it can be released.
std::unique_ptr<DiskFindBootState> SetupBootDisk(efi::MockBootServices& mock_services,
                                                 efi_disk_io_protocol* disk_io_protocol);

}  // namespace gigaboot

#endif  // SRC_FIRMWARE_GIGABOOT_SRC_DISKIO_TEST_H_
