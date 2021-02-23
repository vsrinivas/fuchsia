// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diskio.h"

#include <lib/efi/testing/fake_disk_io_protocol.h>
#include <lib/efi/testing/stub_boot_services.h>
#include <zircon/hw/gpt.h>

#include <memory>
#include <utility>
#include <vector>

#include <efi/boot-services.h>
#include <efi/protocol/block-io.h>
#include <efi/protocol/device-path.h>
#include <efi/protocol/disk-io.h>
#include <efi/protocol/loaded-image.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

// Equality operator so we can do EXPECT_EQ() with gpt_entry_t.
// Has to be in the global namespace.
bool operator==(const gpt_entry_t& a, const gpt_entry_t& b) {
  return memcmp(&a, &b, sizeof(a)) == 0;
}

namespace {

using testing::_;
using testing::DoAll;
using testing::ElementsAreArray;
using testing::Return;
using testing::SetArgPointee;

// Arbitrary values chosen for testing, these can be modified if needed.
// The block size just has to be 8-byte aligned for easy casting.
constexpr uint32_t kBootMediaId = 3;
constexpr uint32_t kBootMediaBlockSize = 512;
constexpr uint64_t kBootMediaNumBlocks = 8;
constexpr uint64_t kBootMediaSize = kBootMediaBlockSize * kBootMediaNumBlocks;
static_assert(kBootMediaBlockSize % 8 == 0, "Block size must be 8-byte aligned");

// These values don't matter, they're just arbitrary handles, but make them
// somewhat recognizable so that if a failure occurs it's easy to tell which
// one it's referring to.
const efi_handle kImageHandle = reinterpret_cast<efi_handle>(0x10);
const efi_handle kDeviceHandle = reinterpret_cast<efi_handle>(0x20);
const efi_handle kBlockHandle = reinterpret_cast<efi_handle>(0x30);

// A set of partitions we can use to set up a fake GPT.
// These are broken out as individual variables as well to make it easy to
// grab GUIDs when needed.
const gpt_entry_t kZirconAGptEntry = {
    .type = GPT_ZIRCON_ABR_TYPE_GUID,
    .guid = {0x01},
    .first = 3,
    .last = 4,
    // Partition names are little-endian UTF-16.
    .name = "z\0i\0r\0c\0o\0n\0_\0a\0",
};
const gpt_entry_t kZirconBGptEntry = {
    .type = GPT_ZIRCON_ABR_TYPE_GUID,
    .guid = {0x02},
    .first = 5,
    .last = 6,
    .name = "z\0i\0r\0c\0o\0n\0_\0b\0",
};
const gpt_entry_t kZirconRGptEntry = {
    .type = GPT_ZIRCON_ABR_TYPE_GUID,
    .guid = {0x03},
    .first = 7,
    .last = 8,
    .name = "z\0i\0r\0c\0o\0n\0_\0r\0",
};
const gpt_entry_t kFvmGptEntry = {
    .type = GPT_FVM_TYPE_GUID,
    .guid = {0x04},
    .first = 9,
    .last = 11,
    .name = "f\0v\0m\0",
};

std::vector<gpt_entry_t> TestPartitions() {
  return std::vector<gpt_entry_t>{
      // Defined out-of-order to make sure our code handles it properly.
      kFvmGptEntry,
      kZirconAGptEntry,
      kZirconBGptEntry,
      kZirconRGptEntry,
  };
}

// Returns a disk_t with reasonable default values to represent the boot media.
disk_t TestBootDisk(efi_disk_io_protocol* disk_protocol, efi_boot_services* boot_services) {
  return disk_t{
      .io = disk_protocol,
      .h = kBlockHandle,
      .bs = boot_services,
      .img = kImageHandle,
      .first = 0,
      .last = kBootMediaNumBlocks - 1,
      .blksz = kBootMediaBlockSize,
      .id = kBootMediaId,
  };
}

const uint8_t kUnknownPartitionGuid[GPT_GUID_LEN] = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0xFF};

class MockBootServices : public efi::StubBootServices {
 public:
  MOCK_METHOD(efi_status, OpenProtocol,
              (efi_handle handle, efi_guid* protocol, void** intf, efi_handle agent_handle,
               efi_handle controller_handle, uint32_t attributes),
              (override));
  MOCK_METHOD(efi_status, CloseProtocol,
              (efi_handle handle, efi_guid* protocol, efi_handle agent_handle,
               efi_handle controller_handle),
              (override));
  MOCK_METHOD(efi_status, LocateHandleBuffer,
              (efi_locate_search_type search_type, efi_guid* protocol, void* search_key,
               size_t* num_handles, efi_handle** buf),
              (override));
};

// Matcher for an efi_guid.
//
// We have to alias to <efi_guid> type explicitly because the compiler can't
// deduce a struct type from an aggregate initializer. Providing the template
// type explicitly allows us to use the matcher on GUID constants e.g.:
//   EXPECT_CALL(..., Guid(EFI_FOO_PROTOCOL_GUID), ...);
MATCHER_P(GuidT, guid, "") { return memcmp(&guid, arg, sizeof(guid)) == 0; }
#define Guid GuidT<efi_guid>

// Registers an expectation that |protocol_guid| will be opened on |handle|.
// |protocol| will be set for the caller.
void ExpectProtocolOpenOnly(MockBootServices& mock, efi_handle handle, efi_guid protocol_guid,
                            void* protocol) {
  EXPECT_CALL(mock, OpenProtocol(handle, Guid(protocol_guid), _, _, _, _))
      .WillOnce(DoAll(SetArgPointee<2>(protocol), Return(EFI_SUCCESS)));
}

// Registers an expectation that |protocol_guid| will be opened and closed on
// |handle|.
void ExpectProtocol(MockBootServices& mock, efi_handle handle, efi_guid protocol_guid,
                    void* protocol) {
  ExpectProtocolOpenOnly(mock, handle, protocol_guid, protocol);
  EXPECT_CALL(mock, CloseProtocol(handle, Guid(protocol_guid), _, _)).WillOnce(Return(EFI_SUCCESS));
}

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
      .DeviceHandle = kDeviceHandle,
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

// Performs all the necessary mocking so that disk_find_boot() will complete
// successfully.
//
// The returned object holds the state necessary for the mocks and must be kept
// in scope until disk_find_boot() is called, after which it can be released.
std::unique_ptr<DiskFindBootState> ExpectDiskFindBoot(MockBootServices& mock_services,
                                                      efi_disk_io_protocol* disk_io_protocol) {
  auto state = std::make_unique<DiskFindBootState>();

  ExpectProtocol(mock_services, kImageHandle, EFI_LOADED_IMAGE_PROTOCOL_GUID, &state->loaded_image);
  ExpectProtocol(mock_services, kDeviceHandle, EFI_DEVICE_PATH_PROTOCOL_GUID, &state->device_path);

  // LocateHandleBuffer() dynamically allocates the list of handles, we need to
  // do the same since the caller will try to free it when finished.
  efi_handle* handle_buffer = reinterpret_cast<efi_handle*>(malloc(sizeof(efi_handle)));
  handle_buffer[0] = kBlockHandle;
  EXPECT_CALL(mock_services, LocateHandleBuffer(_, Guid(EFI_BLOCK_IO_PROTOCOL_GUID), _, _, _))
      .WillOnce(DoAll(SetArgPointee<3>(1), SetArgPointee<4>(handle_buffer), Return(EFI_SUCCESS)));

  ExpectProtocol(mock_services, kBlockHandle, EFI_BLOCK_IO_PROTOCOL_GUID, &state->block_io);
  ExpectProtocol(mock_services, kBlockHandle, EFI_DEVICE_PATH_PROTOCOL_GUID, &state->device_path);

  // The disk I/O protocol shouldn't close since it's returned to the caller.
  ExpectProtocolOpenOnly(mock_services, kBlockHandle, EFI_DISK_IO_PROTOCOL_GUID, disk_io_protocol);

  return state;
}

TEST(DiskFindBoot, Success) {
  MockBootServices mock_services;
  efi::FakeDiskIoProtocol fake_disk;
  auto state = ExpectDiskFindBoot(mock_services, fake_disk.protocol());

  efi_system_table system_table = {.BootServices = mock_services.services()};
  disk_t result = {};
  EXPECT_EQ(0, disk_find_boot(kImageHandle, &system_table, false, &result));

  // Make sure the fetched information was properly copied out to the disk_t.
  EXPECT_EQ(result.io, fake_disk.protocol());
  EXPECT_EQ(result.h, kBlockHandle);
  EXPECT_EQ(result.bs, mock_services.services());
  EXPECT_EQ(result.img, kImageHandle);
  EXPECT_EQ(result.first, 0u);
  EXPECT_EQ(result.last, kBootMediaNumBlocks - 1);
  EXPECT_EQ(result.blksz, kBootMediaBlockSize);
  EXPECT_EQ(result.id, kBootMediaId);
}

// Writes a primary GPT to |fake_disk| such that it will contain the given
// |partitions|. Partition contents on disk are unchanged.
//
// This will use blocks 0-2 for MBR/header/partition data, so a
// properly-configured set of partitions should only use blocks in the range
// [3, kBootMediaNumBlocks).
//
// Should be called with ASSERT_NO_FATAL_FAILURES().
void SetupDiskPartitions(efi::FakeDiskIoProtocol& fake_disk,
                         const std::vector<gpt_entry_t>& partitions) {
  std::vector<uint8_t>& contents = fake_disk.contents(kBootMediaId);
  contents.resize(kBootMediaSize);

  // Fake disk contents are heap-allocated so will always be properly aligned.
  // Find the header pointer at block 1.
  gpt_header_t* header = reinterpret_cast<gpt_header_t*>(&contents[kBootMediaBlockSize]);
  *header = {
      .magic = GPT_MAGIC,
      .revision = 0,
      .size = GPT_HEADER_SIZE,
      .crc32 = 0,  // Gigaboot doesn't check CRCs yet.
      .reserved0 = 0,
      .current = 1,
      .backup = 0,  // No backup GPT support yet.
      .first = 3,
      .last = kBootMediaNumBlocks - 1,
      .guid = {},
      .entries = 2,
      .entries_count = static_cast<uint32_t>(partitions.size()),
      .entries_size = GPT_ENTRY_SIZE,
      .entries_crc = 0,
  };

  // Copy in the GPT entry array.
  // For simplicitly, only allow a single block's worth of partition entries.
  size_t total_entry_size = partitions.size() * sizeof(partitions[0]);
  ASSERT_LE(total_entry_size, kBootMediaBlockSize);
  memcpy(&contents[kBootMediaBlockSize * header->entries], partitions.data(), total_entry_size);

  // Make sure the media has declared enough space for all the partitions.
  ASSERT_LT(header->last, kBootMediaNumBlocks);
}

TEST(DiskFindPartition, ByType) {
  MockBootServices mock_services;
  efi::FakeDiskIoProtocol fake_disk_protocol;
  ASSERT_NO_FATAL_FAILURE(SetupDiskPartitions(fake_disk_protocol, TestPartitions()));

  const disk_t disk = TestBootDisk(fake_disk_protocol.protocol(), mock_services.services());
  gpt_entry_t partition = {};

  // Success.
  EXPECT_EQ(0, disk_find_partition(&disk, false, kFvmGptEntry.type, nullptr, nullptr, &partition));
  EXPECT_EQ(kFvmGptEntry, partition);

  // Failure due to no matches.
  EXPECT_NE(0,
            disk_find_partition(&disk, false, kUnknownPartitionGuid, nullptr, nullptr, &partition));

  // Failure due to multiple matches (zircon_{a,b,r} have the same type GUID).
  EXPECT_NE(0,
            disk_find_partition(&disk, false, kZirconAGptEntry.type, nullptr, nullptr, &partition));
}

TEST(DiskFindPartition, ByGuid) {
  MockBootServices mock_services;
  efi::FakeDiskIoProtocol fake_disk_protocol;

  // Duplicate the zircon_a GUID so we can test multiple match failure.
  auto partitions = TestPartitions();
  memcpy(partitions[2].guid, kZirconAGptEntry.guid, sizeof(kZirconAGptEntry.guid));
  ASSERT_NO_FATAL_FAILURE(SetupDiskPartitions(fake_disk_protocol, partitions));

  const disk_t disk = TestBootDisk(fake_disk_protocol.protocol(), mock_services.services());
  gpt_entry_t partition = {};

  // Success.
  EXPECT_EQ(0, disk_find_partition(&disk, false, nullptr, kFvmGptEntry.guid, nullptr, &partition));
  EXPECT_EQ(kFvmGptEntry, partition);

  // Failure due to no matches.
  EXPECT_NE(0,
            disk_find_partition(&disk, false, nullptr, kUnknownPartitionGuid, nullptr, &partition));

  // Failure due to multiple matches.
  EXPECT_NE(0,
            disk_find_partition(&disk, false, nullptr, kZirconAGptEntry.guid, nullptr, &partition));
}

TEST(DiskFindPartition, ByName) {
  MockBootServices mock_services;
  efi::FakeDiskIoProtocol fake_disk_protocol;

  // Duplicate the zircon_a name so we can test multiple match failure.
  auto partitions = TestPartitions();
  memcpy(partitions[2].name, kZirconAGptEntry.name, sizeof(kZirconAGptEntry.name));
  ASSERT_NO_FATAL_FAILURE(SetupDiskPartitions(fake_disk_protocol, partitions));

  const disk_t disk = TestBootDisk(fake_disk_protocol.protocol(), mock_services.services());
  gpt_entry_t partition = {};

  // Success.
  EXPECT_EQ(0, disk_find_partition(&disk, false, nullptr, nullptr, "fvm", &partition));
  EXPECT_EQ(kFvmGptEntry, partition);

  // Failure due to no matches.
  EXPECT_NE(0, disk_find_partition(&disk, false, nullptr, nullptr, "unknown", &partition));

  // Failure due to multiple matches.
  EXPECT_NE(0, disk_find_partition(&disk, false, nullptr, nullptr, "zircon_a", &partition));
}

TEST(DiskFindPartition, ByAll) {
  MockBootServices mock_services;
  efi::FakeDiskIoProtocol fake_disk_protocol;
  ASSERT_NO_FATAL_FAILURE(SetupDiskPartitions(fake_disk_protocol, TestPartitions()));

  const disk_t disk = TestBootDisk(fake_disk_protocol.protocol(), mock_services.services());
  gpt_entry_t partition = {};

  // Success.
  EXPECT_EQ(0, disk_find_partition(&disk, false, kFvmGptEntry.type, kFvmGptEntry.guid, "fvm",
                                   &partition));
  EXPECT_EQ(kFvmGptEntry, partition);

  // Failure due to param mismatches.
  EXPECT_NE(0, disk_find_partition(&disk, false, kFvmGptEntry.type, kFvmGptEntry.guid, "zircon_a",
                                   &partition));
  EXPECT_NE(0, disk_find_partition(&disk, false, kFvmGptEntry.type, kZirconAGptEntry.guid, "fvm",
                                   &partition));
  EXPECT_NE(0, disk_find_partition(&disk, false, kZirconAGptEntry.type, kFvmGptEntry.guid, "fvm",
                                   &partition));
}

TEST(DiskFindPartition, Verbose) {
  MockBootServices mock_services;
  efi::FakeDiskIoProtocol fake_disk_protocol;
  ASSERT_NO_FATAL_FAILURE(SetupDiskPartitions(fake_disk_protocol, TestPartitions()));

  const disk_t disk = TestBootDisk(fake_disk_protocol.protocol(), mock_services.services());
  gpt_entry_t partition = {};

  // We don't need to check the verbose output, just make sure it doesn't
  // crash and still gives the expected result.
  EXPECT_EQ(
      0, disk_find_partition(&disk, true, kFvmGptEntry.type, kFvmGptEntry.guid, "fvm", &partition));
  EXPECT_EQ(kFvmGptEntry, partition);
}

TEST(DiskFindPartition, SkipInvalidPartitions) {
  MockBootServices mock_services;
  efi::FakeDiskIoProtocol fake_disk_protocol;

  auto partitions = TestPartitions();
  partitions[0].first = 0;
  partitions[1].last = 0;
  partitions[2].first = partitions[2].last + 1;
  ASSERT_NO_FATAL_FAILURE(SetupDiskPartitions(fake_disk_protocol, partitions));

  const disk_t disk = TestBootDisk(fake_disk_protocol.protocol(), mock_services.services());
  gpt_entry_t partition = {};

  // Match any partition by passing all null filters. This should skip
  // partitions 0-2 and only find partitions 3.
  EXPECT_EQ(0, disk_find_partition(&disk, false, nullptr, nullptr, nullptr, &partition));
  EXPECT_EQ(partitions[3], partition);
}

TEST(PartitionTypeGuid, KnownPartitionNames) {
  const std::pair<const char*, const std::vector<uint8_t>> known_partitions[] = {
      {"zircon_a", GUID_ZIRCON_A_VALUE}, {"zircon-a", GUID_ZIRCON_A_VALUE},
      {"zircon_b", GUID_ZIRCON_B_VALUE}, {"zircon-b", GUID_ZIRCON_B_VALUE},
      {"zircon_r", GUID_ZIRCON_R_VALUE}, {"zircon-r", GUID_ZIRCON_R_VALUE},
      {"vbmeta_a", GUID_VBMETA_A_VALUE}, {"vbmeta_b", GUID_VBMETA_B_VALUE},
      {"vbmeta_r", GUID_VBMETA_R_VALUE}, {"bootloader", GUID_EFI_VALUE},
      {"fuchsia-esp", GUID_EFI_VALUE},
  };

  for (const auto& [name, expected_guid] : known_partitions) {
    const uint8_t* type_guid = partition_type_guid(name);
    EXPECT_THAT(expected_guid, ElementsAreArray(type_guid, GPT_GUID_LEN));
  }
}

TEST(PartitionTypeGuid, UnknownPartitionName) {
  EXPECT_EQ(nullptr, partition_type_guid(""));
  EXPECT_EQ(nullptr, partition_type_guid("unknown_partition"));
  EXPECT_EQ(nullptr, partition_type_guid("zircon_a_with_suffix"));
}

struct IsUsbBootState {
  efi_device_path_protocol device_path[2] = {
      {
          .Type = DEVICE_PATH_MESSAGING,
          .SubType = DEVICE_PATH_MESSAGING_USB,
          .Length = {sizeof(efi_device_path_protocol), 0},
      },
      {
          .Type = DEVICE_PATH_END,
          .SubType = DEVICE_PATH_END,
          .Length = {sizeof(efi_device_path_protocol), 0},
      },
  };

  efi_loaded_image_protocol loaded_image = {
      .DeviceHandle = kDeviceHandle,
      .FilePath = device_path,
  };
};

std::unique_ptr<IsUsbBootState> ExpectUsbBootState(MockBootServices& mock_services,
                                                   efi_disk_io_protocol* disk_io_protocol) {
  auto state = std::make_unique<IsUsbBootState>();

  ExpectProtocol(mock_services, kImageHandle, EFI_LOADED_IMAGE_PROTOCOL_GUID, &state->loaded_image);
  ExpectProtocol(mock_services, kDeviceHandle, EFI_DEVICE_PATH_PROTOCOL_GUID, &state->device_path);

  return state;
}

TEST(IsBootFromUsb, ReturnsTrue) {
  MockBootServices mock_services;
  efi::FakeDiskIoProtocol fake_disk;
  auto state = ExpectUsbBootState(mock_services, fake_disk.protocol());

  efi_system_table system_table = {.BootServices = mock_services.services()};
  EXPECT_TRUE(is_booting_from_usb(kImageHandle, &system_table));
}

TEST(IsBootFromUsb, ReturnsFalse) {
  MockBootServices mock_services;
  efi::FakeDiskIoProtocol fake_disk;
  auto state = ExpectUsbBootState(mock_services, fake_disk.protocol());
  state->device_path[0].SubType = DEVICE_PATH_MESSAGING_ATAPI;

  efi_system_table system_table = {.BootServices = mock_services.services()};
  EXPECT_FALSE(is_booting_from_usb(kImageHandle, &system_table));
}

}  // namespace
