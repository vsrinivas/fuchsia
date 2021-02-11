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
#include <efi/protocol/disk-io.h>
#include <efi/protocol/loaded-image.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

namespace {

using testing::_;
using testing::DoAll;
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

TEST(DiskFindPartition, SinglePartitionSuccess) {
  MockBootServices mock_services;
  efi::FakeDiskIoProtocol fake_disk_protocol;

  std::vector<gpt_entry_t> partitions = {{.type = GPT_ZIRCON_ABR_TYPE_GUID, .first = 3, .last = 5}};
  ASSERT_NO_FATAL_FAILURE(SetupDiskPartitions(fake_disk_protocol, partitions));

  disk_t disk = {
      .io = fake_disk_protocol.protocol(),
      .h = kBlockHandle,
      .bs = mock_services.services(),
      .img = kImageHandle,
      .first = 0,
      .last = kBootMediaNumBlocks - 1,
      .blksz = kBootMediaBlockSize,
      .id = kBootMediaId,
  };
  EXPECT_EQ(0, disk_find_partition(&disk, false, partitions[0].type, nullptr));
  EXPECT_EQ(3u, disk.first);
  EXPECT_EQ(5u, disk.last);
}

TEST(DiskFindPartition, MultiPartitionSuccess) {
  MockBootServices mock_services;
  efi::FakeDiskIoProtocol fake_disk_protocol;

  std::vector<gpt_entry_t> partitions = {{.type = GPT_ZIRCON_ABR_TYPE_GUID, .first = 3, .last = 5},
                                         {.type = GPT_VBMETA_ABR_TYPE_GUID, .first = 8, .last = 8},
                                         {.type = GPT_FVM_TYPE_GUID, .first = 6, .last = 7}};
  ASSERT_NO_FATAL_FAILURE(SetupDiskPartitions(fake_disk_protocol, partitions));

  const disk_t disk = {
      .io = fake_disk_protocol.protocol(),
      .h = kBlockHandle,
      .bs = mock_services.services(),
      .img = kImageHandle,
      .first = 0,
      .last = kBootMediaNumBlocks - 1,
      .blksz = kBootMediaBlockSize,
      .id = kBootMediaId,
  };
  // disk_find_partition() returns the result by overwriting disk_t fields,
  // make a copy so we can call it again afterwards using the original.
  disk_t disk_copy = disk;
  EXPECT_EQ(0, disk_find_partition(&disk_copy, false, partitions[0].type, nullptr));
  EXPECT_EQ(3u, disk_copy.first);
  EXPECT_EQ(5u, disk_copy.last);

  disk_copy = disk;
  EXPECT_EQ(0, disk_find_partition(&disk_copy, false, partitions[1].type, nullptr));
  EXPECT_EQ(8u, disk_copy.first);
  EXPECT_EQ(8u, disk_copy.last);

  disk_copy = disk;
  EXPECT_EQ(0, disk_find_partition(&disk_copy, false, partitions[2].type, nullptr));
  EXPECT_EQ(6u, disk_copy.first);
  EXPECT_EQ(7u, disk_copy.last);
}

TEST(DiskFindPartition, UnknownPartitionFailure) {
  MockBootServices mock_services;
  efi::FakeDiskIoProtocol fake_disk_protocol;

  std::vector<gpt_entry_t> partitions = {{.type = GPT_ZIRCON_ABR_TYPE_GUID, .first = 3, .last = 5},
                                         {.type = GPT_VBMETA_ABR_TYPE_GUID, .first = 8, .last = 8},
                                         {.type = GPT_FVM_TYPE_GUID, .first = 6, .last = 7}};
  ASSERT_NO_FATAL_FAILURE(SetupDiskPartitions(fake_disk_protocol, partitions));

  disk_t disk = {
      .io = fake_disk_protocol.protocol(),
      .h = kBlockHandle,
      .bs = mock_services.services(),
      .img = kImageHandle,
      .first = 0,
      .last = kBootMediaNumBlocks - 1,
      .blksz = kBootMediaBlockSize,
      .id = kBootMediaId,
  };
  uint8_t unknown_guid[GPT_GUID_LEN] = GPT_FACTORY_TYPE_GUID;
  EXPECT_NE(0, disk_find_partition(&disk, false, unknown_guid, nullptr));
}

TEST(GuidValueFromName, KnownPartitionNames) {
  const std::pair<const char*, const std::vector<uint8_t>> known_partitions[] = {
      {"zircon-a", GUID_ZIRCON_A_VALUE}, {"zircon-b", GUID_ZIRCON_B_VALUE},
      {"zircon-r", GUID_ZIRCON_R_VALUE}, {"vbmeta_a", GUID_VBMETA_A_VALUE},
      {"vbmeta_b", GUID_VBMETA_B_VALUE}, {"vbmeta_r", GUID_VBMETA_R_VALUE},
      {"fuchsia-esp", GUID_EFI_VALUE},
  };

  for (const auto& [name, expected_guid] : known_partitions) {
    std::vector<uint8_t> guid(GPT_GUID_LEN);
    EXPECT_EQ(0, guid_value_from_name(name, guid.data()));
    EXPECT_EQ(expected_guid, guid);
  }
}

TEST(GuidValueFromName, UnknownPartitionName) {
  std::vector<uint8_t> guid(GPT_GUID_LEN);
  EXPECT_NE(0, guid_value_from_name("unknown_partition", guid.data()));
}

}  // namespace
