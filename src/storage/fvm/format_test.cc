// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/fvm/format.h"

#include <zircon/errors.h>

#include <sstream>
#include <string_view>

#include <zxtest/zxtest.h>

#include "src/lib/uuid/uuid.h"
#include "src/storage/fvm/fvm.h"

namespace fvm {

namespace {

bool StringBeginsWith(const std::string_view& str, const std::string_view& begins_with) {
  if (str.size() < begins_with.size())
    return false;
  return str.substr(0, begins_with.size()) == begins_with;
}

}  // namespace

TEST(FvmFormat, DefaultInitializedGetterValues) {
  Header header;

  // Currently the partition table has a constant size so we always return it. Arguably we could
  // also return 0 for these.
  EXPECT_EQ(kBlockSize, header.GetPartitionTableOffset());
  EXPECT_EQ(kMaxVPartitions - 1, header.GetPartitionTableEntryCount());
  EXPECT_EQ(65536, header.GetPartitionTableByteSize());

  // The allocation table starts after the partition table and is empty. If we change the partition
  // table getters to return 0 in this case, the allocation table offset could also be changed.
  EXPECT_EQ(header.GetPartitionTableOffset() + header.GetPartitionTableByteSize(),
            header.GetAllocationTableOffset());
  EXPECT_EQ(0u, header.GetAllocationTableUsedEntryCount());
  EXPECT_EQ(0u, header.GetAllocationTableUsedByteSize());
  EXPECT_EQ(0u, header.GetAllocationTableAllocatedEntryCount());
  EXPECT_EQ(0u, header.GetAllocationTableAllocatedByteSize());

  EXPECT_EQ(0u, header.GetMetadataUsedBytes());
  EXPECT_EQ(0u, header.GetMetadataAllocatedBytes());
}

TEST(FvmFormat, SliceConstructors) {
  constexpr size_t kInitialSliceCount = 2;
  constexpr size_t kMaxSliceCount = 4096;
  constexpr size_t kSmallSliceSize = kBlockSize;
  Header header = Header::FromGrowableSliceCount(kMaxUsablePartitions, kInitialSliceCount,
                                                 kMaxSliceCount, kSmallSliceSize);
  EXPECT_EQ(kInitialSliceCount, header.GetAllocationTableUsedEntryCount());
  // The constructor guarantees only that the table is "big enough" to handle the required slices,
  // but it could be larger depending on padding.
  EXPECT_LT(kMaxSliceCount, header.GetAllocationTableAllocatedEntryCount());
  EXPECT_EQ(kSmallSliceSize, header.slice_size);
  EXPECT_EQ(header.GetSliceDataOffset(1) + kSmallSliceSize * kInitialSliceCount,
            header.fvm_partition_size);
}

TEST(FvmFormat, SizeConstructors) {
  // A growable partition that starts off with no slices.
  constexpr size_t kInitialDiskSize = 1;  // Too small for anything.
  constexpr size_t kMaxDiskSize = static_cast<size_t>(1024) * 1024 * 1024 * 1024;  // 1TB
  constexpr size_t kBigSliceSize = 1024 * 1024;
  Header header = Header::FromGrowableDiskSize(kMaxUsablePartitions, kInitialDiskSize, kMaxDiskSize,
                                               kBigSliceSize);
  // No allocated slices since it's too small.
  EXPECT_EQ(0, header.GetAllocationTableUsedEntryCount());
  EXPECT_LT(kMaxDiskSize / kBigSliceSize, header.GetAllocationTableAllocatedEntryCount());
  EXPECT_EQ(kBigSliceSize, header.slice_size);

  size_t metadata_size = header.GetSliceDataOffset(1);

  // Test an input disk size that's one too small for two slices. The slice count should always
  // be rounded down so there are only full slices, so we should be left with one current slicw.
  size_t round_down_disk_size = metadata_size + kBigSliceSize * 2 - 1;
  header = Header::FromGrowableDiskSize(kMaxUsablePartitions, round_down_disk_size, kMaxDiskSize,
                                        kBigSliceSize);
  EXPECT_EQ(1, header.GetAllocationTableUsedEntryCount());
  EXPECT_EQ(metadata_size + kBigSliceSize, header.fvm_partition_size);

  // A large non-growable disk. This one has block size == slice size so all of the disk should
  // be addressable with no rounding.
  constexpr size_t kSmallSliceSize = kBlockSize;
  header = Header::FromDiskSize(kMaxUsablePartitions, kMaxDiskSize, kSmallSliceSize);
  EXPECT_LT(kMaxDiskSize / kSmallSliceSize, header.GetAllocationTableAllocatedEntryCount());
  EXPECT_EQ(kMaxDiskSize, header.fvm_partition_size);
}

TEST(FvmFormat, Getters) {
  constexpr size_t kUsedSlices = 5;
  Header header = Header::FromSliceCount(kMaxUsablePartitions, kUsedSlices, kBlockSize * 2);

  // The partition table starts at the block following the superblock.
  EXPECT_EQ(kBlockSize, header.GetPartitionTableOffset());

  // The number of usable entries in the partition table is one less that the number of slots.
  // TODO(fxb/59980) make this consistent so we can use the whole table. Either use 0-1023 as the
  // valid partition range, or 1-1024.
  EXPECT_EQ(header.vpartition_table_size / sizeof(VPartitionEntry) - 1,
            header.GetPartitionTableEntryCount());

  // The byte size is trivial. Currently this is fixed.
  // TODO(fxb/40192): Use this value so the partition table can have different sizes:
  //   EXPECT_EQ(header.vpartition_table_size, header.GetPartitionTableByteSize());
  EXPECT_EQ(65536, header.GetPartitionTableByteSize());

  // The allocation table follows the partition table. The allocated byte size just comes from the
  // header directly.
  EXPECT_EQ(kBlockSize + 65536, header.GetAllocationTableOffset());
  EXPECT_EQ(header.allocation_table_size, header.GetAllocationTableAllocatedByteSize());

  // The number of usable entries in the table is one less than the number that will fix.
  // TODO(fxb/59980) use all the slots:
  //   EXPECT_EQ(header.allocation_table_size / sizeof(SliceEntry),
  //             header.GetAllocationTableAllocatedEntryCount());
  EXPECT_EQ(header.allocation_table_size / sizeof(SliceEntry) - 1,
            header.GetAllocationTableAllocatedEntryCount());

  // The number of used slices. The bytes covered is rounded up the next block size.
  EXPECT_EQ(kUsedSlices, header.GetAllocationTableUsedEntryCount());
  EXPECT_EQ(kBlockSize, header.GetAllocationTableUsedByteSize());

  // The full metadata covers to the end of the allocation table.
  EXPECT_EQ(header.GetAllocationTableOffset() + header.GetAllocationTableUsedByteSize(),
            header.GetMetadataUsedBytes());
  EXPECT_EQ(header.GetAllocationTableOffset() + header.GetAllocationTableAllocatedByteSize(),
            header.GetMetadataAllocatedBytes());

  // The max usable entries for a disk is capped at the allocated entry count.
  EXPECT_EQ(0u, header.GetMaxAllocationTableEntriesForDiskSize(0));
  EXPECT_EQ(header.GetAllocationTableUsedEntryCount(),
            header.GetMaxAllocationTableEntriesForDiskSize(header.fvm_partition_size));
  EXPECT_EQ(header.GetAllocationTableAllocatedEntryCount(),
            header.GetMaxAllocationTableEntriesForDiskSize(header.slice_size * 1024 * 1024));
}

TEST(FvmFormat, IsValid) {
  constexpr uint64_t kMaxDiskSize = std::numeric_limits<uint64_t>::max();

  // 0-initialized header is invalid.
  Header header;
  std::string error_message;
  EXPECT_FALSE(header.IsValid(kMaxDiskSize, kBlockSize, error_message));

  // Normal valid header.
  Header valid_header = Header::FromDiskSize(kMaxUsablePartitions, 1028 * 1024 * 1024, 8192);
  EXPECT_TRUE(valid_header.IsValid(kMaxDiskSize, kBlockSize, error_message));

  // Magic is incorrect.
  header = valid_header;
  header.magic++;
  EXPECT_FALSE(header.IsValid(kMaxDiskSize, kBlockSize, error_message));
  EXPECT_TRUE(StringBeginsWith(error_message, "Bad magic value for FVM header.\n"));

  // Version too new.
  header = valid_header;
  header.format_version = kCurrentFormatVersion + 1;
  EXPECT_FALSE(header.IsValid(kMaxDiskSize, kBlockSize, error_message));
  EXPECT_TRUE(StringBeginsWith(error_message, "Header format version does not match fvm driver"));

  // Slice count overflow.
  header = valid_header;
  header.slice_size = kMaxSliceSize + kBlockSize;
  EXPECT_FALSE(header.IsValid(kMaxDiskSize, kBlockSize, error_message));
  EXPECT_TRUE(StringBeginsWith(error_message, "Slice size would overflow 64 bits"));

  // Slice size overflow.
  header = valid_header;
  header.pslice_count = kMaxVSlices + 1;
  EXPECT_FALSE(header.IsValid(kMaxDiskSize, kBlockSize, error_message));
  EXPECT_TRUE(StringBeginsWith(error_message, "Slice count is greater than the max (2147483648)"));

  // Slice size invalid.
  header = valid_header;
  header.slice_size = 13;
  EXPECT_FALSE(header.IsValid(kMaxDiskSize, kBlockSize, error_message));
  EXPECT_TRUE(StringBeginsWith(
      error_message, "Slice size is not a multiple of the underlying disk's block size (8192)"));

  // Allocation table size too small.
  header = valid_header;
  header.pslice_count = 1024 * 1024;  // Requires lots of allocation table entries.
  header.allocation_table_size = kBlockSize;
  EXPECT_FALSE(header.IsValid(16384, kBlockSize, error_message));
  EXPECT_TRUE(StringBeginsWith(error_message, "Expected allocation table to be at least"));

  // Data won't fit on the disk.
  header = valid_header;
  header.fvm_partition_size = 1024 * 1024 + kBlockSize;
  EXPECT_FALSE(header.IsValid(header.fvm_partition_size - kBlockSize, kBlockSize, error_message));
  EXPECT_TRUE(StringBeginsWith(error_message,
                               "Block device (1048576 bytes) too small for fvm_partition_size"));
}

TEST(FvmFormat, HasValidTableSizes) {
  // A 0-initialized header is invalid, the partition table must have a fixed size.
  Header header;
  std::string error_message;
  EXPECT_FALSE(header.HasValidTableSizes(error_message));
  EXPECT_EQ(
      "Bad vpartition table size.\n"
      "FVM Header\n"
      "  magic: 6075990659671348806\n"
      "  format_version: 1\n"
      "  pslice_count: 0\n"
      "  slice_size: 0\n"
      "  fvm_partition_size: 0\n"
      "  vpartition_table_size: 0\n"
      "  allocation_table_size: 0\n"
      "  generation: 0\n"
      "  oldest_revision: 1\n",
      error_message);

  // Normal valid header.
  header = Header::FromDiskSize(kMaxUsablePartitions, 1028 * 1024 * 1024, 8192);
  EXPECT_TRUE(header.HasValidTableSizes(error_message));

  // Allocation table needs to be an even multiple.
  header.allocation_table_size--;
  EXPECT_FALSE(header.HasValidTableSizes(error_message));
  EXPECT_TRUE(StringBeginsWith(error_message, "Bad allocation table size"));

  // Allocation table is too large.
  header.allocation_table_size = kMaxAllocationTableByteSize + kBlockSize;
  EXPECT_FALSE(header.HasValidTableSizes(error_message));
  EXPECT_TRUE(StringBeginsWith(error_message, "Bad allocation table size"));
}

TEST(VPartitionEntry, DefaultConstructor) {
  VPartitionEntry def;
  EXPECT_FALSE(def.IsAllocated());
  EXPECT_TRUE(def.IsActive());
  EXPECT_TRUE(def.IsFree());
  EXPECT_EQ("", def.name());
}

TEST(VPartitionEntry, Constructor) {
  uint8_t type[kGuidSize];
  std::fill(std::begin(type), std::end(type), '1');

  uint8_t guid[kGuidSize];
  std::fill(std::begin(guid), std::end(guid), '2');

  const char kName[] = "Name";
  const uint32_t kSlices = 345;

  VPartitionEntry entry(type, guid, kSlices, kName);
  EXPECT_TRUE(std::equal(std::begin(type), std::end(type), std::begin(entry.type)));
  EXPECT_TRUE(std::equal(std::begin(guid), std::end(guid), std::begin(entry.guid)));

  EXPECT_EQ(kName, entry.name());
}

TEST(VPartitionEntry, StringFromArray) {
  constexpr size_t kLen = 8;
  uint8_t buf[kLen] = {0};
  EXPECT_TRUE(VPartitionEntry::StringFromArray(buf).empty());

  buf[0] = 'a';
  std::string str = VPartitionEntry::StringFromArray(buf);
  ASSERT_EQ(1u, str.size());
  EXPECT_EQ('a', str[0]);

  // Not null terminated.
  std::fill(std::begin(buf), std::end(buf), 'b');
  str = VPartitionEntry::StringFromArray(buf);
  ASSERT_EQ(kLen, str.size());
  EXPECT_EQ("bbbbbbbb", str);
}

TEST(VPartitionEntry, ToString) {
  std::stringstream out;

  out << VPartitionEntry();
  EXPECT_EQ(
      "\"\" slices:0 flags:0 (act=1) type:00000000-0000-0000-0000-000000000000 "
      "guid:00000000-0000-0000-0000-000000000000",
      out.str());

  uuid::Uuid type{0x01, 0x02, 0x03, 0x04, 0x04, 0x05, 0x06, 0x07,
                  0x08, 0x09, 0x0a, 0X0b, 0X0c, 0X0d, 0X0e, 0X0f};
  uuid::Uuid guid{0x11, 0x12, 0x13, 0x14, 0x14, 0x15, 0x16, 0x17,
                  0x18, 0x19, 0x1a, 0X1b, 0X1c, 0X1d, 0X1e, 0X1f};

  VPartitionEntry allocated(type.bytes(), guid.bytes(), 3, "my partition");
  allocated.SetActive(true);

  out.str(std::string());
  out << allocated;
  EXPECT_EQ(
      "\"my partition\" slices:3 flags:0 (act=1) type:04030201-0504-0706-0809-0a0b0c0d0e0f "
      "guid:14131211-1514-1716-1819-1a1b1c1d1e1f",
      out.str());
}

TEST(SliceEntry, ToString) {
  std::stringstream out;

  out << SliceEntry();
  EXPECT_EQ("SliceEntry(<free>)", out.str());

  out.str(std::string());
  SliceEntry used;
  used.Set(12, 19);
  out << used;
  EXPECT_EQ("SliceEntry(vpartition=12, vslice=19)", out.str());
}

}  // namespace fvm
