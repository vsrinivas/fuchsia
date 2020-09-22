// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gpt-tests.h"

#include <ctype.h>
#include <fuchsia/hardware/block/c/fidl.h>
#include <lib/cksum.h>
#include <lib/fdio/cpp/caller.h>
#include <zircon/assert.h>

#include <memory>
#include <optional>

#include <fbl/auto_call.h>
#include <gpt/gpt.h>
#include <gpt/guid.h>
#include <ramdevice-client/ramdisk.h>
#include <zxtest/zxtest.h>

#include "gpt.h"

extern bool gUseRamDisk;
extern char gDevPath[PATH_MAX];
extern unsigned int gRandSeed;

namespace gpt {
namespace {

using gpt::GptDevice;
using gpt::KnownGuid;

constexpr uint64_t kHoleSize = 10;

uuid::Uuid TestGuid(uint32_t id) {
  return uuid::Uuid(uuid::RawUuid{id, 0x10, 0x20, 0x30, 0x40, {1, 2, 3, 4, 5, 6}});
}

// Return a copy of the input variable.
//
// Useful when the input value is a packed field and needs to be passed to
// a function taking a const reference. For example, given:
//
//    void Bar(const int& b);
//
//    struct Foo {
//      char a;
//      int b;
//    } PACKED;
//
//  we cannot directly call "Bar(foo->b)", but can call "Bar(Unpack(foo->b))".
template <typename T>
inline T Unpack(T val) {
  return val;
}

// generate a random number between [0, max)
uint64_t random_length(uint64_t max) { return (rand_r(&gRandSeed) % max); }

constexpr uint64_t partition_size(const gpt_partition_t* p) { return p->last - p->first + 1; }

void UpdateHeaderCrcs(gpt_header_t* header, uint8_t* entries_array, size_t size) {
  header->entries_crc = crc32(0, entries_array, size);
  header->crc32 = 0;
  header->crc32 = crc32(0, reinterpret_cast<uint8_t*>(header), sizeof(*header));
}

void destroy_gpt(int fd, uint64_t block_size, uint64_t offset, uint64_t block_count) {
  char zero[block_size];
  memset(zero, 0, sizeof(zero));

  ASSERT_GT(block_count, 0, "Block count should be greater than zero");
  ASSERT_GT(block_size, 0, "Block count should be greater than zero");

  uint64_t first = offset;
  uint64_t last = offset + block_count - 1;

  for (uint64_t i = first; i <= last; i++) {
    ASSERT_EQ(pwrite(fd, zero, sizeof(zero), block_size * i), (ssize_t)sizeof(zero),
              "Failed to pwrite");
  }
  // fsync is not supported in rpc-server.cpp
  // TODO(fxbug.dev/33099) to fix this
  // ASSERT_EQ(fsync(fd), 0, "Failed to fsync");
}

// This class keeps track of what we expect partitions to be on the
// GptDevice. Before making the change to GptDevice, we make tracking
// changes to this class so that we can verify a set of changes.
class Partitions {
 public:
  Partitions(uint32_t count, uint64_t first, uint64_t last);

  // Returns partition at index index. Returns null if index is out of range.
  const gpt_partition_t* GetPartition(uint32_t index) const;

  // Returns number of parition created/removed.
  uint32_t GetCount() const { return partition_count_; }

  // Marks a partition as created in GPT.
  void MarkCreated(uint32_t index);

  // Mark a partition as removed in GPT.
  void ClearCreated(uint32_t index);

  // Returns true if the GPT should have the partition.
  bool IsCreated(uint32_t index) const;

  // Returns number of partition that should exist on GPT.
  uint32_t CreatedCount() const;

  // Returns true if two partitions are the same.
  bool Compare(const gpt_partition_t* in_mem_partition,
               const gpt_partition_t* on_disk_partition) const;

  // Returns true if the partition p exists in partitions_.
  bool Find(const gpt_partition_t* p, uint32_t* out_index) const;

  // Changes gpt_partition_t.type. One of the fields in the GUID
  // is incremented.
  void ChangePartitionType(uint32_t partition_index);

  // Changes gpt_partition_t.guid. One of the fields in the GUID
  // is incremented.
  void ChangePartitionGuid(uint32_t partition_index);

  // Sets the visibility attribute of the partition
  void SetPartitionVisibility(uint32_t partition_index, bool visible);

  // Changes the range a partition covers. The function doesn't check if these
  // changes are valid or not (whether they over lap with other partitions,
  // cross device limits)
  void ChangePartitionRange(uint32_t partition_index, uint64_t start, uint64_t end);

  // Gets the current value of gpt_partition_t.flags
  void GetPartitionFlags(uint32_t partition_index, uint64_t* flags) const;

  // Sets the current value of gpt_partition_t.flags
  void SetPartitionFlags(uint32_t partition_index, uint64_t flags);

 private:
  // List of partitions
  gpt_partition_t partitions_[gpt::kPartitionCount];

  // A variable to track whether a partition is created on GPT or not
  bool created_[gpt::kPartitionCount] = {};

  // Number of partitions_ that is populated with valid information
  uint32_t partition_count_;
};

Partitions::Partitions(uint32_t count, uint64_t first, uint64_t last) {
  ZX_ASSERT(count > 0);
  ZX_ASSERT(count <= gpt::kPartitionCount);
  partition_count_ = count;

  uint64_t part_first = first, part_last;
  uint64_t part_max_len = (last - first) / partition_count_;
  ZX_ASSERT(part_max_len > 0);

  memset(partitions_, 0, sizeof(partitions_));
  for (uint32_t i = 0; i < partition_count_; i++) {
    part_last = part_first + random_length(part_max_len);
    uuid::Uuid guid = TestGuid(i);
    memcpy(partitions_[i].type, guid.bytes(), sizeof(partitions_[i].type));
    memcpy(partitions_[i].guid, guid.bytes(), sizeof(partitions_[i].type));
    partitions_[i].first = part_first;
    partitions_[i].last = part_last;
    partitions_[i].flags = 0;
    memset(partitions_[i].name, 0, sizeof(partitions_[i].name));
    snprintf(reinterpret_cast<char*>(partitions_[i].name), sizeof(partitions_[i].name), "%u_part",
             i);

    // Set next first block
    part_first += part_max_len;

    // Previous last block should be less than next first block
    ZX_ASSERT(part_last < part_first);
  }
}

const gpt_partition_t* Partitions::GetPartition(uint32_t index) const {
  if (index >= partition_count_) {
    return nullptr;
  }

  return &partitions_[index];
}

void Partitions::MarkCreated(uint32_t index) {
  ASSERT_LT(index, partition_count_);
  created_[index] = true;
}

void Partitions::ClearCreated(uint32_t index) {
  ASSERT_LT(index, partition_count_);
  created_[index] = false;
}

bool Partitions::IsCreated(uint32_t index) const { return created_[index]; }

uint32_t Partitions::CreatedCount() const {
  uint32_t created_count = 0;

  for (uint32_t i = 0; i < GetCount(); i++) {
    if (IsCreated(i)) {
      created_count++;
    }
  }
  return (created_count);
}

bool Partitions::Compare(const gpt_partition_t* in_mem_partition,
                         const gpt_partition_t* on_disk_partition) const {
  if (memcmp(in_mem_partition->type, on_disk_partition->type, sizeof(in_mem_partition->type)) !=
      0) {
    return false;
  }

  if (memcmp(in_mem_partition->guid, on_disk_partition->guid, sizeof(in_mem_partition->guid)) !=
      0) {
    return false;
  }

  if (in_mem_partition->first != on_disk_partition->first) {
    return false;
  }

  if (in_mem_partition->last != on_disk_partition->last) {
    return false;
  }

  if (in_mem_partition->flags != on_disk_partition->flags) {
    return false;
  }

  // In mem partition name is a c-string whereas on-disk partition name
  // is stored as UTF-16. We need to convert UTF-16 to c-string before we
  // compare.
  char name[GPT_NAME_LEN];
  memset(name, 0, GPT_NAME_LEN);
  utf16_to_cstring(name, (const uint16_t*)on_disk_partition->name, GPT_NAME_LEN / 2);

  if (strncmp(name, reinterpret_cast<const char*>(in_mem_partition->name), GPT_NAME_LEN / 2) != 0) {
    return false;
  }

  return true;
}

bool Partitions::Find(const gpt_partition_t* p, uint32_t* out_index) const {
  for (uint32_t i = 0; i < partition_count_; i++) {
    if (Compare(GetPartition(i), p)) {
      *out_index = i;
      return true;
    }
  }

  return false;
}

void IncrementGuid(uint8_t* guid) { guid[6]++; }

void Partitions::ChangePartitionGuid(uint32_t partition_index) {
  ASSERT_LT(partition_index, partition_count_);
  IncrementGuid(partitions_[partition_index].guid);
}

void Partitions::ChangePartitionType(uint32_t partition_index) {
  ASSERT_LT(partition_index, partition_count_);
  IncrementGuid(partitions_[partition_index].type);
}

void Partitions::SetPartitionVisibility(uint32_t partition_index, bool visible) {
  ASSERT_LT(partition_index, partition_count_);
  gpt::SetPartitionVisibility(&partitions_[partition_index], visible);
}

void Partitions::ChangePartitionRange(uint32_t partition_index, uint64_t start, uint64_t end) {
  ASSERT_LT(partition_index, partition_count_);
  partitions_[partition_index].first = start;
  partitions_[partition_index].last = end;
}

void Partitions::GetPartitionFlags(uint32_t partition_index, uint64_t* flags) const {
  ASSERT_LT(partition_index, partition_count_);
  *flags = partitions_[partition_index].flags;
}

void Partitions::SetPartitionFlags(uint32_t partition_index, uint64_t flags) {
  ASSERT_LT(partition_index, partition_count_);
  partitions_[partition_index].flags = flags;
}

class LibGptTestFixture : public zxtest::Test {
 public:
  LibGptTest* GetLibGptTest() const { return lib_gpt_test_.get(); }

 protected:
  void SetUp() override {
    LibGptTest::Options options{};
    if (gUseRamDisk) {
      lib_gpt_test_ = LibGptTest::Create(options);
    } else {
      options.disk_path = std::string(gDevPath);
      lib_gpt_test_ = LibGptTest::Create(options);
    }
  }

 private:
  std::unique_ptr<LibGptTest> lib_gpt_test_ = {};
};

using GptDeviceTest = LibGptTestFixture;

void LibGptTest::Reset() {
  std::unique_ptr<GptDevice> gpt;

  // explicitly close the fd, if open, before we attempt to reopen it.
  fd_.reset();

  fd_.reset(open(disk_path_, O_RDWR));

  ASSERT_TRUE(fd_.is_valid(), "Could not open block device\n");
  ASSERT_OK(GptDevice::Create(fd_.get(), GetBlockSize(), GetBlockCount(), &gpt));
  gpt_ = std::move(gpt);
}

void LibGptTest::Finalize() {
  ASSERT_FALSE(gpt_->Valid(), "Valid GPT on uninitialized disk");

  ASSERT_OK(gpt_->Finalize(), "Failed to finalize");
  ASSERT_TRUE(gpt_->Valid(), "Invalid GPT after finalize");
}

void LibGptTest::Sync() {
  ASSERT_OK(gpt_->Sync(), "Failed to sync");
  ASSERT_TRUE(gpt_->Valid(), "Invalid GPT after sync");
}

void LibGptTest::ReadRange() {
  ASSERT_EQ(gpt_->Range(&usable_start_block_, &usable_last_block_), ZX_OK,
            "Retrieval of device range failed.");

  // TODO(auradkar): GptDevice doesn't export api to get GPT-metadata size.
  // If it does, we can keep better track of metadata size it says it needs
  // and metadata it actually uses.
  ASSERT_LT(GetUsableStartBlock(), GetBlockCount(), "Range starts after EOD");
  ASSERT_LT(GetUsableStartBlock(), GetUsableLastBlock(), "Invalid range");
  ASSERT_LT(GetUsableLastBlock(), GetBlockCount(), "Range end greater than block count");
  ASSERT_GT(GetUsableBlockCount(), 0, "GPT occupied all available blocks");
}

zx_status_t LibGptTest::ReadMbr(mbr::Mbr* mbr) const {
  ZX_ASSERT(sizeof(*mbr) <= blk_size_);

  // Read the block containing the MBR.
  char buff[blk_size_];
  ssize_t ret = pread(fd_.get(), buff, blk_size_, 0);
  if (ret < 0) {
    return ZX_ERR_IO;
  }

  // Copy the result to "mbr".
  memcpy(mbr, buff, sizeof(*mbr));
  return ZX_OK;
}

void LibGptTest::PrepDisk(bool sync) {
  if (sync) {
    Sync();
  } else {
    Finalize();
  }

  ReadRange();
}

void LibGptTest::InitDisk(const char* disk_path) {
  snprintf(disk_path_, PATH_MAX, "%s", disk_path);
  fbl::unique_fd fd(open(disk_path_, O_RDWR));
  ASSERT_TRUE(fd.is_valid(), "Could not open block device to fetch info");
  fdio_cpp::UnownedFdioCaller disk_caller(fd.get());
  fuchsia_hardware_block_BlockInfo block_info;
  zx_status_t status;
  ASSERT_EQ(fuchsia_hardware_block_BlockGetInfo(disk_caller.borrow_channel(), &status, &block_info),
            ZX_OK);
  ASSERT_OK(status);

  blk_size_ = block_info.block_size;
  blk_count_ = block_info.block_count;

  ASSERT_GE(GetDiskSize(), kAccptableMinimumSize, "Insufficient disk space for tests");
  fd_ = std::move(fd);
}

void LibGptTest::InitRamDisk(const LibGptTest::Options& options) {
  ASSERT_EQ(ramdisk_create(options.block_size, options.block_count, &ramdisk_), ZX_OK,
            "Could not create ramdisk");
  InitDisk(ramdisk_get_path(ramdisk_));
}

std::unique_ptr<LibGptTest> LibGptTest::Create(const LibGptTest::Options& options) {
  // Use "new" to access private constructor.
  auto result = std::unique_ptr<LibGptTest>(new LibGptTest());

  // Set up disk.
  if (options.disk_path.empty()) {
    result->InitRamDisk(options);
  } else {
    result->InitDisk(options.disk_path.c_str());
  }

  // TODO(auradkar): All tests assume that the disks don't have an initialized
  // disk. If tests find an GPT initialized disk at the beginning of test,
  // they fail. The tests leave disks in initalized state.
  //
  // To either uninitialize an initialized disk as a part of setup
  // test needs to know where gpt lies on the disk. As of now libgpt doesn't
  // export an api to get the location(s) of the gpt on disk. So, we assume
  // here that gpt lies in first few (GptMetadataBlocksCount()) blocks on the
  // device. We also ignore any backup copies on the device.
  // Once there exists an api in libgpt to get size and location(s) of gpt,
  // we can setup/cleanup before/after running tests in a better way.
  destroy_gpt(result->fd_.get(), result->GetBlockSize(), 0, result->GptMetadataBlocksCount());

  result->Reset();

  return result;
}

LibGptTest::~LibGptTest() {
  if (ramdisk_ != nullptr) {
    ramdisk_destroy(ramdisk_);
  }
}

uint64_t EntryArrayBlockCount(uint64_t block_size) {
  return ((kMaxPartitionTableSize + block_size - 1) / block_size);
}

// Manually calculate the minimum block count.
uint64_t GptMinimumBlockCount(uint64_t block_size) {
  uint64_t block_count = kPrimaryHeaderStartBlock;

  // Two copies of gpt_header_t. A block for each.
  block_count += (2 * kHeaderBlocks);

  // Two copies of entries array.
  block_count += (2 * EntryArrayBlockCount(block_size));

  // We need at least one block as usable block.
  return block_count + 1;
}

// Returns a copy of |str| with any hex values converted to uppercase.
std::string HexToUpper(std::string str) {
  for (char& ch : str) {
    if (ch >= 'a' && ch <= 'f') {
      ch -= 'a';
      ch += 'A';
    }
  }
  return str;
}

}  // namespace

// Creates "partitions->GetCount()"" number of partitions on GPT.
//  The information needed to create a partitions is passed in "partitions".
void AddPartitionHelper(LibGptTest* libGptTest, Partitions* partitions) {
  ASSERT_GT(partitions->GetCount(), 0, "At least one partition is required");
  for (uint32_t i = 0; i < partitions->GetCount(); i++) {
    const gpt_partition_t* p = partitions->GetPartition(i);
    ASSERT_EQ(libGptTest->AddPartition(reinterpret_cast<const char*>(p->name), p->type, p->guid,
                                       p->first, partition_size(p), p->flags),
              ZX_OK, "Add partition failed");
    partitions->MarkCreated(i);
  }
}

// Removes randomly selected "remove_count" number of partitions.
void RemovePartitionsHelper(LibGptTest* libGptTest, Partitions* partitions, uint32_t remove_count) {
  uint32_t index;

  ASSERT_LE(remove_count, partitions->GetCount(), "Remove count exceeds whats available");
  ASSERT_LE(remove_count, partitions->CreatedCount(), "Cannot remove more partitions than created");

  for (uint32_t i = 0; i < remove_count; i++) {
    while (true) {
      index = static_cast<uint32_t>(rand_r(&gRandSeed)) % partitions->GetCount();
      if (partitions->IsCreated(index)) {
        break;
      }
    }
    ASSERT_TRUE(partitions->IsCreated(index), "Partition already removed");
    const gpt_partition_t* p = partitions->GetPartition(index);
    ASSERT_OK(libGptTest->RemovePartition(p->guid), "Failed to remove partition");
    partitions->ClearCreated(index);
  }
}

// Verifies all the partitions that exists on GPT are the ones that are created
// by the test and vice-versa.
void PartitionVerify(LibGptTest* libGptTest, const Partitions* partitions) {
  bool found[gpt::kPartitionCount] = {};
  uint32_t found_index = 0;

  // Check what's found on disk is created by us
  // iteratre over all partition that are present on disk and make sure
  // that we intended to create them.
  // Note: The index of an entry/partition need not match with the index of
  // the partition in "Partition* partition".
  for (uint32_t i = 0; i < gpt::kPartitionCount; i++) {
    gpt_partition_t* p = libGptTest->GetPartition(i);

    if (p == NULL) {
      continue;
    }

    ASSERT_TRUE(partitions->Find(p, &found_index), "Found an entry on GPT that we did not create");

    ASSERT_TRUE(partitions->IsCreated(found_index), "Removed entry reincarnated");
    found[found_index] = true;
  }

  // Check what's created is found on disk
  for (uint32_t i = 0; i < partitions->GetCount(); i++) {
    if (partitions->IsCreated(i)) {
      ASSERT_TRUE(found[i], "Created partition is missing on disk");
    }
  }
}

// Creates partitions and verifies them.
void AddPartitions(LibGptTest* libGptTest, Partitions* partitions, bool sync) {
  AddPartitionHelper(libGptTest, partitions);

  if (sync) {
    libGptTest->Sync();
  }

  PartitionVerify(libGptTest, partitions);
  ASSERT_EQ(partitions->GetCount(), partitions->CreatedCount());
}

// Removes partitions and verifies them.
void RemovePartitions(LibGptTest* libGptTest, Partitions* partitions, uint32_t remove_count,
                      bool sync) {
  RemovePartitionsHelper(libGptTest, partitions, remove_count);
  if (sync) {
    libGptTest->Sync();
  }

  PartitionVerify(libGptTest, partitions);
  ASSERT_EQ(partitions->GetCount() - partitions->CreatedCount(), remove_count,
            "Not as many removed as we wanted to");
}

// Removes all partitions and verifies them.
void RemoveAllPartitions(LibGptTest* libGptTest, Partitions* partitions, bool sync) {
  ASSERT_LE(partitions->GetCount(), partitions->CreatedCount(), "Not all partitions populated");
  ASSERT_OK(libGptTest->RemoveAllPartitions(), "Failed to remove all partition");

  for (uint32_t i = 0; i < partitions->GetCount(); i++) {
    partitions->ClearCreated(i);
  }

  PartitionVerify(libGptTest, partitions);
  ASSERT_EQ(partitions->CreatedCount(), 0, "Not as many removed as we wanted to");
}

// Test adding total_partitions partitions to GPT w/o writing to disk
void AddPartitionTestHelper(LibGptTest* libGptTest, uint32_t total_partitions, bool sync) {
  libGptTest->PrepDisk(sync);
  Partitions partitions(total_partitions, libGptTest->GetUsableStartBlock(),
                        libGptTest->GetUsableLastBlock());

  AddPartitions(libGptTest, &partitions, sync);
}

// Test adding total_partitions partitions to GPT and test removing remove_count
// partitions later w/o writing to disk.
void RemovePartition(LibGptTest* libGptTest, uint32_t total_partitions, uint32_t remove_count,
                     bool sync) {
  libGptTest->PrepDisk(sync);
  Partitions partitions(total_partitions, libGptTest->GetUsableStartBlock(),
                        libGptTest->GetUsableLastBlock());

  AddPartitions(libGptTest, &partitions, sync);
  RemovePartitions(libGptTest, &partitions, remove_count, sync);
}

// Test removing all total_partititions from GPT w/o syncing.
void RemoveAllPartitions(LibGptTest* libGptTest, uint32_t total_partitions, bool sync) {
  libGptTest->PrepDisk(sync);

  Partitions partitions(total_partitions, libGptTest->GetUsableStartBlock(),
                        libGptTest->GetUsableLastBlock());

  AddPartitions(libGptTest, &partitions, sync);
  RemoveAllPartitions(libGptTest, &partitions, sync);
}

void SetPartitionTypeTestHelper(LibGptTest* libGptTest, uint32_t total_partitions, bool sync) {
  libGptTest->PrepDisk(sync);

  Partitions partitions(total_partitions, libGptTest->GetUsableStartBlock(),
                        libGptTest->GetUsableLastBlock());

  AddPartitions(libGptTest, &partitions, sync);

  // Change partition type in cached copy so that we can verify the
  // changes in GptDevice
  uint32_t index = static_cast<uint32_t>(rand_r(&gRandSeed) % total_partitions);
  partitions.ChangePartitionType(index);

  // Keep a backup copy of GptDevice's partition type
  const gpt_partition_t* p = libGptTest->GetPartition(index);
  uuid::Uuid before(p->type);

  // Change the type in GptDevice
  EXPECT_OK(libGptTest->SetPartitionType(index, partitions.GetPartition(index)->type));

  // Get the changes
  p = libGptTest->GetPartition(index);
  uuid::Uuid after(p->type);

  // The type should have changed by now in GptDevice
  EXPECT_NE(before, after);

  PartitionVerify(libGptTest, &partitions);
}

void SetPartitionGuidTestHelper(LibGptTest* libGptTest, uint32_t total_partitions, bool sync) {
  libGptTest->PrepDisk(sync);

  Partitions partitions(total_partitions, libGptTest->GetUsableStartBlock(),
                        libGptTest->GetUsableLastBlock());

  AddPartitions(libGptTest, &partitions, sync);

  // Change partition id in cached copy so that we can verify the
  // changes in GptDevice
  uint32_t index = static_cast<uint32_t>(rand_r(&gRandSeed) % total_partitions);
  partitions.ChangePartitionGuid(index);

  // Keep a backup copy of GptDevice's partition ID
  const gpt_partition_t* p = libGptTest->GetPartition(index);
  uuid::Uuid before(p->guid);

  // Change the guid in GptDevice
  EXPECT_OK(libGptTest->SetPartitionGuid(index, partitions.GetPartition(index)->guid));

  // Get the changes
  p = libGptTest->GetPartition(index);
  uuid::Uuid after(p->guid);

  // The guid should have changed by now in GptDevice
  EXPECT_NE(before, after);

  PartitionVerify(libGptTest, &partitions);
}

// Find a partition that has a hole between it's end and start of next partition
zx_status_t FindPartitionToExpand(const Partitions* partitions, uint32_t* out_index,
                                  uint64_t* out_first, uint64_t* out_last) {
  for (uint32_t index = 0; index < partitions->GetCount(); index++) {
    if (index == partitions->GetCount() - 1) {
      *out_last = partitions->GetPartition(index)->last + kHoleSize;
      *out_index = index;
      *out_first = partitions->GetPartition(index)->first;
      return ZX_OK;
    }

    // if delta between last block of this partition and first next
    // partition is greater than one then we have found a hole
    if ((partitions->GetPartition(index + 1)->first - partitions->GetPartition(index)->last) > 1) {
      *out_last = partitions->GetPartition(index + 1)->first - 1;
      *out_index = index;
      *out_first = partitions->GetPartition(index)->first;
      return ZX_OK;
    }
  }
  return ZX_ERR_NOT_FOUND;
}

// Find a partition that has a hole between it's end and start of next partition
zx_status_t FindPartitionToShrink(const Partitions* partitions, uint32_t* out_index,
                                  uint64_t* out_first, uint64_t* out_last) {
  constexpr uint64_t kMinPartitionSize = 10;

  for (uint32_t index = 0; index < partitions->GetCount(); index++) {
    // The partition needs to have at least kMinPartitionSize to shrink
    if ((partitions->GetPartition(index)->last - partitions->GetPartition(index)->first) >
        kMinPartitionSize) {
      *out_last = partitions->GetPartition(index)->last - 2;
      *out_first = partitions->GetPartition(index)->first + 2;
      *out_index = index;
      return ZX_OK;
    }
  }

  return ZX_ERR_NOT_FOUND;
}

// Find partition tries to find a partition that can be either expanded or shrunk.
// Partition first and last block number can either increase or decrease. So there are
// 8 combinations of how a partition boundary can change. We test two of those 8 cases.
// On successfully finding a partition that can be modified, function return ZX_OK
// On success The output parameters
//  - out_index is the partition that can be modified.
//  - out_first is new value of partition's first block.
//  - out_last is the new value of partition's last black.
typedef zx_status_t (*find_partition_t)(const Partitions* partitions, uint32_t* out_index,
                                        uint64_t* out_first, uint64_t* out_last);

void SetPartitionRangeTestHelper(LibGptTest* libGptTest, uint32_t total_partitions, bool sync,
                                 find_partition_t find_part) {
  uint64_t new_last = 0, new_first = 0;

  ASSERT_GT(total_partitions, 1, "For range to test we need at least two partition");

  libGptTest->PrepDisk(sync);

  Partitions partitions(total_partitions, libGptTest->GetUsableStartBlock(),
                        libGptTest->GetUsableLastBlock() - kHoleSize);

  AddPartitions(libGptTest, &partitions, sync);

  uint32_t index;
  EXPECT_OK(find_part(&partitions, &index, &new_first, &new_last));

  ASSERT_NE(index, partitions.GetCount(), "Could not find a hole");
  ASSERT_NE(new_first, 0, "Could not find a hole to change range");
  ASSERT_NE(new_last, 0, "Could not find a hole to change range");

  partitions.ChangePartitionRange(index, new_first, new_last);

  // Change the range in GptDevice
  EXPECT_OK(libGptTest->SetPartitionRange(index, new_first, new_last));

  // Get the changes
  gpt_partition_t* p = libGptTest->GetPartition(index);
  EXPECT_EQ(p->first, new_first, "First doesn't match after update");
  EXPECT_EQ(p->last, new_last, "Last doesn't match after update");

  PartitionVerify(libGptTest, &partitions);
}

void PartitionVisibilityFlip(LibGptTest* libGptTest, Partitions* partitions, uint32_t index) {
  // Get the current visibility and flip it
  const gpt_partition_t* p = libGptTest->GetPartition(index);
  bool visible = gpt::IsPartitionVisible(p);
  visible = !visible;
  partitions->SetPartitionVisibility(index, visible);

  // Change the guid in GptDevice
  EXPECT_OK(libGptTest->SetPartitionVisibility(index, visible));

  // Get the changes and verify
  p = libGptTest->GetPartition(index);
  EXPECT_EQ(gpt::IsPartitionVisible(p), visible);
  PartitionVerify(libGptTest, partitions);
}

void PartitionVisibilityTestHelper(LibGptTest* libGptTest, uint32_t total_partitions, bool sync) {
  libGptTest->PrepDisk(sync);

  Partitions partitions(total_partitions, libGptTest->GetUsableStartBlock(),
                        libGptTest->GetUsableLastBlock());

  AddPartitions(libGptTest, &partitions, sync);
  uint32_t index = static_cast<uint32_t>(rand_r(&gRandSeed) % total_partitions);

  PartitionVisibilityFlip(libGptTest, &partitions, index);
  PartitionVisibilityFlip(libGptTest, &partitions, index);
}

void PartitionFlagsFlip(LibGptTest* libGptTest, Partitions* partitions, uint32_t index) {
  // Get the current flags
  uint64_t old_flags, updated_flags;
  ASSERT_OK(libGptTest->GetPartitionFlags(index, &old_flags), "");

  uint64_t new_flags = ~old_flags;
  partitions->SetPartitionFlags(index, new_flags);

  // Change the flags
  ASSERT_OK(libGptTest->SetPartitionFlags(index, new_flags), "");

  // Get the changes and verify
  ASSERT_OK(libGptTest->GetPartitionFlags(index, &updated_flags), "");
  ASSERT_EQ(new_flags, updated_flags, "Flags update failed");
  PartitionVerify(libGptTest, partitions);
}

void PartitionFlagsTestHelper(LibGptTest* libGptTest, uint32_t total_partitions, bool sync) {
  libGptTest->PrepDisk(sync);

  Partitions partitions(total_partitions, libGptTest->GetUsableStartBlock(),
                        libGptTest->GetUsableLastBlock());

  AddPartitions(libGptTest, &partitions, sync);
  uint32_t index = static_cast<uint32_t>(rand_r(&gRandSeed) % total_partitions);

  PartitionFlagsFlip(libGptTest, &partitions, index);
  PartitionFlagsFlip(libGptTest, &partitions, index);
}

// Test if Diffs after adding partitions reflect all the changes.
void DiffsTestHelper(LibGptTest* libGptTest, uint32_t total_partitions) {
  uint32_t diffs;

  EXPECT_NE(libGptTest->GetDiffs(0, &diffs), ZX_OK, "GetDiffs should fail before PrepDisk");
  libGptTest->PrepDisk(false);
  EXPECT_NE(libGptTest->GetDiffs(0, &diffs), ZX_OK,
            "GetDiffs for non-existing partition should fail");

  Partitions partitions(total_partitions, libGptTest->GetUsableStartBlock(),
                        libGptTest->GetUsableLastBlock());
  AddPartitions(libGptTest, &partitions, false);
  EXPECT_OK(libGptTest->GetDiffs(0, &diffs), "Diffs zero after adding partition");

  EXPECT_EQ(diffs,
            gpt::kGptDiffType | gpt::kGptDiffGuid | gpt::kGptDiffFirst | gpt::kGptDiffLast |
                gpt::kGptDiffName,
            "Unexpected diff after creating partition");
  libGptTest->Sync();
  EXPECT_OK(libGptTest->GetDiffs(0, &diffs), "");
  EXPECT_EQ(diffs, 0, "Diffs not zero after syncing partition");
}

uint64_t ComputePerCopySize(uint64_t block_size) {
  return block_size + (kPartitionCount * kEntrySize);
}

uint64_t ComputePerCopyBlockCount(uint64_t block_size) {
  return (ComputePerCopySize(block_size) + block_size - 1) / block_size;
}

uint64_t ComputeMinimumBlockDeviceSize(uint64_t block_size) {
  // One mbr block.
  return 1 + (2 * ComputePerCopyBlockCount(block_size));
}

TEST(MinimumBytesPerCopyTest, BlockSizeTooSmall) {
  ASSERT_EQ(ZX_ERR_INVALID_ARGS, MinimumBytesPerCopy(kHeaderSize - 1).error());
}

TEST(MinimumBytesPerCopyTest, BlockSizeDefaultBlockSize) {
  ASSERT_EQ(ComputePerCopySize(kBlockSize), MinimumBytesPerCopy(kBlockSize).value());
}

TEST(MinimumBytesPerCopyTest, BlockSize1Meg) {
  ASSERT_EQ(ComputePerCopySize(1 << 20), MinimumBytesPerCopy(1 << 20).value());
}

TEST(MinimumBlocksPerCopyTest, BlockSizeTooSmall) {
  ASSERT_EQ(ZX_ERR_INVALID_ARGS, MinimumBlocksPerCopy(kHeaderSize - 1).error());
}

TEST(MinimumBlocksPerCopyTest, BlockSizeDefaultBlockSize) {
  ASSERT_EQ(ComputePerCopyBlockCount(kBlockSize), MinimumBlocksPerCopy(kBlockSize).value());
}

TEST(MinimumBlocksPerCopyTest, BlockSize1Meg) {
  ASSERT_EQ(ComputePerCopyBlockCount(1 << 20), MinimumBlocksPerCopy(1 << 20).value());
}

TEST(MinimumBlockDeviceSizeTest, BlockSizeTooSmall) {
  ASSERT_EQ(ZX_ERR_INVALID_ARGS, MinimumBlockDeviceSize(kHeaderSize - 1).error());
}

TEST(MinimumBlockDeviceSizeTest, BlockSizeDefaultBlockSize) {
  ASSERT_EQ(ComputeMinimumBlockDeviceSize(kBlockSize), MinimumBlockDeviceSize(kBlockSize).value());
}

TEST(MinimumBlockDeviceSizeTest, BlockSize1Meg) {
  ASSERT_EQ(ComputeMinimumBlockDeviceSize(1 << 20), MinimumBlockDeviceSize(1 << 20).value());
}

TEST(EntryBlockCountTest, ValidEntry) {
  gpt_entry_t entry = {};
  entry.guid[0] = 1;
  entry.type[0] = 1;
  entry.first = 10;
  entry.last = 20;
  ASSERT_EQ(EntryBlockCount(&entry).value(), 11);
}

TEST(EntryBlockCountTest, UninitializedEntry) {
  gpt_entry_t entry = {};
  ASSERT_EQ(ZX_ERR_NOT_FOUND, EntryBlockCount(&entry).error());
}

TEST(EntryBlockCountTest, NullPointer) {
  ASSERT_EQ(ZX_ERR_INVALID_ARGS, EntryBlockCount(nullptr).error());
}

TEST(EntryBlockCountTest, UninitializedGuid) {
  gpt_entry_t entry = {};
  entry.guid[0] = 0;
  entry.type[0] = 1;
  entry.first = 10;
  entry.last = 20;
  ASSERT_EQ(ZX_ERR_BAD_STATE, EntryBlockCount(&entry).error());
}

TEST(EntryBlockCountTest, UninitializedType) {
  gpt_entry_t entry = {};
  entry.guid[0] = 1;
  entry.type[0] = 0;
  entry.first = 10;
  entry.last = 20;
  ASSERT_EQ(ZX_ERR_BAD_STATE, EntryBlockCount(&entry).error());
}

TEST(EntryBlockCountTest, BadRange) {
  gpt_entry_t entry = {};
  entry.guid[0] = 1;
  entry.type[0] = 1;
  entry.first = 20;
  entry.last = 10;
  ASSERT_EQ(ZX_ERR_BAD_STATE, EntryBlockCount(&entry).error());
}

// Tests if we can create a GptDevice.
TEST_F(GptDeviceTest, ValidGptOnUninitilizedDisk) {
  LibGptTest* libGptTest = GetLibGptTest();

  EXPECT_FALSE(libGptTest->IsGptValid(), "Valid GPT on uninitialized disk");
}

TEST_F(GptDeviceTest, ValidGptAfterResetOnUninitilized) {
  LibGptTest* libGptTest = GetLibGptTest();

  libGptTest->Reset();
  EXPECT_FALSE(libGptTest->IsGptValid(), "Valid GPT after reset");
}

// Tests Finalize initializes GPT in-memory only and doesn't commit to disk.
TEST_F(GptDeviceTest, FinalizeNoSync) {
  LibGptTest* libGptTest = GetLibGptTest();

  libGptTest->Finalize();

  // Finalize initializes GPT but doesn't write changes to disk.
  // Resetting the Test should bring invalid gpt back.
  libGptTest->Reset();
  EXPECT_FALSE(libGptTest->IsGptValid(), "Valid GPT after finalize and reset");
}

// Tests Finalize initializes GPT and writes it to disk.
TEST_F(GptDeviceTest, FinalizeAndSync) {
  auto libGptTest = GetLibGptTest();
  EXPECT_FALSE(libGptTest->IsGptValid());

  // Sync should write changes to disk. Resetting should bring valid gpt back.
  libGptTest->Sync();
  libGptTest->Reset();
  EXPECT_TRUE(libGptTest->IsGptValid());

  // Check the protective MBR that was written to disk.
  mbr::Mbr mbr;
  zx_status_t status = libGptTest->ReadMbr(&mbr);
  ASSERT_OK(status, "Failed to read MBR");
  EXPECT_EQ(mbr::kMbrBootSignature, mbr.boot_signature, "Invalid MBR boot signature");
  mbr::MbrPartitionEntry expected{
      .status = 0,
      .chs_address_start = {0, 1, 0},
      .type = mbr::kPartitionTypeGptProtective,
      .chs_address_end = {0xff, 0xff, 0xff},
      .start_sector_lba = 1,
      .num_sectors =
          static_cast<uint32_t>(std::min(0xffff'ffffUL, libGptTest->GetBlockCount() - 1)),
  };
  EXPECT_BYTES_EQ(&expected, &mbr.partitions[0], sizeof(expected), "Invalid protective MBR");
}

// Tests the range the GPT blocks falls within disk.
TEST_F(GptDeviceTest, Range) {
  auto libGptTest = GetLibGptTest();
  libGptTest->Finalize();
  libGptTest->ReadRange();
}

TEST_F(GptDeviceTest, AddPartitionNoSync) { AddPartitionTestHelper(GetLibGptTest(), 3, false); }

TEST_F(GptDeviceTest, AddPartition) { AddPartitionTestHelper(GetLibGptTest(), 20, true); }

TEST_F(GptDeviceTest, RemovePartitionNoSync) { RemovePartition(GetLibGptTest(), 12, 4, false); }

TEST_F(GptDeviceTest, RemovePartition) { RemovePartition(GetLibGptTest(), 3, 2, true); }

TEST_F(GptDeviceTest, RemovePartitionRemoveAllOneAtATime) {
  RemovePartition(GetLibGptTest(), 11, 11, false);
}

TEST_F(GptDeviceTest, RemoveAllPartitions) { RemoveAllPartitions(GetLibGptTest(), 12, true); }

TEST_F(GptDeviceTest, RemoveAllPartitionsNoSync) {
  RemoveAllPartitions(GetLibGptTest(), 15, false);
}

TEST_F(GptDeviceTest, SetPartitionType) { SetPartitionTypeTestHelper(GetLibGptTest(), 4, true); }

TEST_F(GptDeviceTest, SetPartitionTypeNoSync) {
  SetPartitionTypeTestHelper(GetLibGptTest(), 8, false);
}

TEST_F(GptDeviceTest, SetPartitionGuidSync) {
  SetPartitionGuidTestHelper(GetLibGptTest(), 5, true);
}

TEST_F(GptDeviceTest, SetPartitionGuidNoSync) {
  SetPartitionGuidTestHelper(GetLibGptTest(), 7, false);
}

TEST_F(GptDeviceTest, ExpandPartitionSync) {
  SetPartitionRangeTestHelper(GetLibGptTest(), 3, true, FindPartitionToExpand);
}

TEST_F(GptDeviceTest, ExpandPartitionNoSync) {
  SetPartitionRangeTestHelper(GetLibGptTest(), 3, false, FindPartitionToExpand);
}

TEST_F(GptDeviceTest, ShrinkPartitionSync) {
  SetPartitionRangeTestHelper(GetLibGptTest(), 3, true, FindPartitionToShrink);
}

TEST_F(GptDeviceTest, ShrinkPartitionNoSync) {
  SetPartitionRangeTestHelper(GetLibGptTest(), 3, false, FindPartitionToShrink);
}

TEST_F(GptDeviceTest, PartitionVisibilityOnSyncTest) {
  PartitionVisibilityTestHelper(GetLibGptTest(), 5, true);
}

TEST_F(GptDeviceTest, PartitionVisibilityNoSyncTest) {
  PartitionVisibilityTestHelper(GetLibGptTest(), 3, false);
}

TEST_F(GptDeviceTest, UpdatePartitionFlagsSync) {
  PartitionFlagsTestHelper(GetLibGptTest(), 9, true);
}

TEST_F(GptDeviceTest, UpdatePartitionFlagsNoSync) {
  PartitionFlagsTestHelper(GetLibGptTest(), 1, false);
}

TEST_F(GptDeviceTest, GetDiffsForAddingOnePartition) { DiffsTestHelper(GetLibGptTest(), 1); }

TEST_F(GptDeviceTest, GetDiffsForAddingMultiplePartition) { DiffsTestHelper(GetLibGptTest(), 9); }

TEST(GptDeviceLoad, ValidHeader) {
  gpt_header_t header = InitializePrimaryHeader(kBlockSize, kBlockCount).value();
  uint8_t blocks[MinimumBytesPerCopy(kBlockSize).value()] = {};
  UpdateHeaderCrcs(&header, &blocks[kBlockSize],
                   MinimumBytesPerCopy(kBlockSize).value() - kBlockSize);
  memcpy(blocks, &header, sizeof(header));
  std::unique_ptr<GptDevice> gpt;
  ASSERT_OK(GptDevice::Load(blocks, MinimumBytesPerCopy(kBlockSize).value(), kBlockSize,
                            kBlockCount, &gpt));
}

TEST(GptDeviceLoad, SmallBlockSize) {
  gpt_header_t header = InitializePrimaryHeader(kBlockSize, kBlockCount).value();
  uint8_t blocks[MinimumBytesPerCopy(kBlockSize).value()] = {};
  memcpy(blocks, &header, sizeof(header));
  std::unique_ptr<GptDevice> gpt;
  ASSERT_EQ(ZX_ERR_INVALID_ARGS, GptDevice::Load(blocks, MinimumBytesPerCopy(kBlockSize).value(),
                                                 kHeaderSize - 1, kBlockCount, &gpt));
}

TEST(GptDeviceLoad, NullGpt) {
  gpt_header_t header = InitializePrimaryHeader(kBlockSize, kBlockCount).value();
  uint8_t blocks[MinimumBytesPerCopy(kBlockSize).value()] = {};
  memcpy(blocks, &header, sizeof(header));
  std::unique_ptr<GptDevice> gpt;
  ASSERT_EQ(ZX_ERR_INVALID_ARGS, GptDevice::Load(blocks, MinimumBytesPerCopy(kBlockSize).value(),
                                                 kHeaderSize, kBlockCount, nullptr));
}

TEST(GptDeviceLoad, NullBuffer) {
  std::unique_ptr<GptDevice> gpt;
  ASSERT_EQ(ZX_ERR_INVALID_ARGS, GptDevice::Load(nullptr, MinimumBytesPerCopy(kBlockSize).value(),
                                                 kHeaderSize, kBlockCount, &gpt));
}

// It is ok to have gpt with no partitions.
TEST(GptDeviceLoadEntries, NoValidEntries) {
  gpt_header_t header = InitializePrimaryHeader(kBlockSize, kBlockCount).value();

  uint8_t blocks[MinimumBytesPerCopy(kBlockSize).value()] = {};
  UpdateHeaderCrcs(&header, &blocks[kBlockSize],
                   MinimumBytesPerCopy(kBlockSize).value() - kBlockSize);
  memcpy(blocks, &header, sizeof(header));
  std::unique_ptr<GptDevice> gpt;
  ASSERT_OK(GptDevice::Load(blocks, MinimumBytesPerCopy(kBlockSize).value(), kBlockSize,
                            kBlockCount, &gpt));
}

TEST(GptDeviceLoadEntries, SmallEntryArray) {
  gpt_header_t header = InitializePrimaryHeader(kBlockSize, kBlockCount).value();

  uint8_t blocks[MinimumBytesPerCopy(kBlockSize).value()] = {};
  UpdateHeaderCrcs(&header, &blocks[kBlockSize],
                   MinimumBytesPerCopy(kBlockSize).value() - kBlockSize);
  memcpy(blocks, &header, sizeof(header));
  std::unique_ptr<GptDevice> gpt;
  ASSERT_EQ(ZX_ERR_BUFFER_TOO_SMALL,
            GptDevice::Load(blocks, MinimumBytesPerCopy(kBlockSize).value() - 1, kBlockSize,
                            kBlockCount, &gpt));
}

TEST(GptDeviceLoadEntries, EntryFirstSmallerThanFirstUsable) {
  gpt_header_t header = InitializePrimaryHeader(kBlockSize, kBlockCount).value();
  std::unique_ptr<uint8_t[]> buffer(new uint8_t[MinimumBytesPerCopy(kBlockSize).value()]());
  uint8_t* blocks = buffer.get();
  gpt_entry_t* entry = reinterpret_cast<gpt_entry_t*>(&blocks[kBlockSize]);

  // last is greater than last usable block.
  entry->guid[0] = 1;
  entry->type[0] = 1;
  entry->first = header.first - 1;
  entry->last = header.last;

  UpdateHeaderCrcs(&header, &blocks[kBlockSize],
                   MinimumBytesPerCopy(kBlockSize).value() - kBlockSize);
  memcpy(blocks, &header, sizeof(header));
  std::unique_ptr<GptDevice> gpt;

  ASSERT_EQ(ZX_ERR_ALREADY_EXISTS, GptDevice::Load(blocks, MinimumBytesPerCopy(kBlockSize).value(),
                                                   kBlockSize, kBlockCount, &gpt));
}

TEST(GptDeviceLoadEntries, EntryLastLargerThanLastUsable) {
  gpt_header_t header = InitializePrimaryHeader(kBlockSize, kBlockCount).value();
  std::unique_ptr<uint8_t[]> buffer(new uint8_t[MinimumBytesPerCopy(kBlockSize).value()]());
  uint8_t* blocks = buffer.get();
  gpt_entry_t* entry = reinterpret_cast<gpt_entry_t*>(&blocks[kBlockSize]);

  // last is greater than last usable block.
  entry->guid[0] = 1;
  entry->type[0] = 1;
  entry->first = header.first;
  entry->last = header.last + 1;

  UpdateHeaderCrcs(&header, &blocks[kBlockSize],
                   MinimumBytesPerCopy(kBlockSize).value() - kBlockSize);
  memcpy(blocks, &header, sizeof(header));
  std::unique_ptr<GptDevice> gpt;

  ASSERT_EQ(ZX_ERR_ALREADY_EXISTS, GptDevice::Load(blocks, MinimumBytesPerCopy(kBlockSize).value(),
                                                   kBlockSize, kBlockCount, &gpt));
}

TEST(GptDeviceLoadEntries, EntryFirstLargerThanEntryLast) {
  gpt_header_t header = InitializePrimaryHeader(kBlockSize, kBlockCount).value();
  std::unique_ptr<uint8_t[]> buffer(new uint8_t[MinimumBytesPerCopy(kBlockSize).value()]());
  uint8_t* blocks = buffer.get();
  gpt_entry_t* entry = reinterpret_cast<gpt_entry_t*>(&blocks[kBlockSize]);

  // last is greater than last usable block.
  entry->guid[0] = 1;
  entry->type[0] = 1;
  entry->first = header.last;
  entry->last = header.first;

  UpdateHeaderCrcs(&header, &blocks[kBlockSize],
                   MinimumBytesPerCopy(kBlockSize).value() - kBlockSize);
  memcpy(blocks, &header, sizeof(header));
  std::unique_ptr<GptDevice> gpt;

  ASSERT_EQ(ZX_ERR_OUT_OF_RANGE, GptDevice::Load(blocks, MinimumBytesPerCopy(kBlockSize).value(),
                                                 kBlockSize, kBlockCount, &gpt));
}

TEST(GptDeviceLoadEntries, EntriesOverlap) {
  gpt_header_t header = InitializePrimaryHeader(kBlockSize, kBlockCount).value();
  std::unique_ptr<uint8_t[]> buffer(new uint8_t[MinimumBytesPerCopy(kBlockSize).value()]());
  uint8_t* blocks = buffer.get();
  gpt_entry_t* entry1 = reinterpret_cast<gpt_entry_t*>(&blocks[kBlockSize]);

  entry1->guid[0] = 1;
  entry1->type[0] = 1;
  entry1->first = header.first;
  entry1->last = kBlockCount / 3;
  EXPECT_TRUE(entry1->first <= entry1->last);

  gpt_entry_t* entry2 = entry1 + 1;
  entry2->guid[0] = 2;
  entry2->type[0] = 2;
  entry2->first = 2 * kBlockCount / 3;  // Block shared with entry1
  entry2->last = header.last;
  EXPECT_TRUE(entry2->first <= entry2->last);

  gpt_entry_t* entry3 = entry2 + 1;
  entry3->guid[0] = 3;
  entry3->type[0] = 3;
  entry3->first = entry1->last;  // Block shared with entry1
  entry3->last = entry2->first - 1;
  EXPECT_TRUE(entry3->first <= entry3->last);

  UpdateHeaderCrcs(&header, &blocks[kBlockSize],
                   MinimumBytesPerCopy(kBlockSize).value() - kBlockSize);
  memcpy(blocks, &header, sizeof(header));
  std::unique_ptr<GptDevice> gpt;
  ASSERT_EQ(ZX_ERR_OUT_OF_RANGE, GptDevice::Load(blocks, MinimumBytesPerCopy(kBlockSize).value(),
                                                 kBlockSize, kBlockCount, &gpt));
}

TEST(GptDeviceLoadEntries, EntryOverlapsWithLastEntry) {
  gpt_header_t header = InitializePrimaryHeader(kBlockSize, kBlockCount).value();

  std::unique_ptr<uint8_t[]> buffer(new uint8_t[MinimumBytesPerCopy(kBlockSize).value()]);
  uint8_t* blocks = buffer.get();
  gpt_entry_t* entry1 = reinterpret_cast<gpt_entry_t*>(&blocks[kBlockSize]);

  entry1->guid[0] = 1;
  entry1->type[0] = 1;
  entry1->first = header.first;
  entry1->last = kBlockCount / 3;
  EXPECT_TRUE(entry1->first <= entry1->last);

  gpt_entry_t* entry2 = entry1 + 1;
  entry2->guid[0] = 2;
  entry2->type[0] = 2;
  entry2->first = 2 * kBlockCount / 3;  // Block shared with entry1
  entry2->last = header.last;
  EXPECT_TRUE(entry2->first <= entry2->last);

  gpt_entry_t* entry3 = entry2 + 1;
  entry3->guid[0] = 3;
  entry3->type[0] = 3;
  entry3->first = entry1->last + 1;
  entry3->last = entry2->first;  // Block shared with entry2
  EXPECT_TRUE(entry3->first <= entry3->last);

  UpdateHeaderCrcs(&header, &blocks[kBlockSize],
                   MinimumBytesPerCopy(kBlockSize).value() - kBlockSize);
  memcpy(blocks, &header, sizeof(header));
  std::unique_ptr<GptDevice> gpt;
  ASSERT_EQ(ZX_ERR_OUT_OF_RANGE, GptDevice::Load(blocks, MinimumBytesPerCopy(kBlockSize).value(),
                                                 kBlockSize, kBlockCount, &gpt));
}

TEST(GptDeviceEntryCountTest, DefaultValue) {
  gpt_header_t header = InitializePrimaryHeader(kBlockSize, kBlockCount).value();
  std::unique_ptr<uint8_t[]> buffer(new uint8_t[MinimumBytesPerCopy(kBlockSize).value()]());
  uint8_t* blocks = buffer.get();

  UpdateHeaderCrcs(&header, &blocks[kBlockSize],
                   MinimumBytesPerCopy(kBlockSize).value() - kBlockSize);
  memcpy(blocks, &header, sizeof(header));
  std::unique_ptr<GptDevice> gpt;
  EXPECT_OK(GptDevice::Load(blocks, MinimumBytesPerCopy(kBlockSize).value(), kBlockSize,
                            kBlockCount, &gpt));
  ASSERT_EQ(gpt->EntryCount(), kPartitionCount);
}

TEST(GptDeviceEntryCountTest, FewerEntries) {
  gpt_header_t header = InitializePrimaryHeader(kBlockSize, kBlockCount).value();
  std::unique_ptr<uint8_t[]> buffer(new uint8_t[MinimumBytesPerCopy(kBlockSize).value()]());
  uint8_t* blocks = buffer.get();

  uint32_t entry_count = 4;
  header.entries_count = entry_count;
  UpdateHeaderCrcs(&header, &blocks[kBlockSize],
                   MinimumBytesPerCopy(kBlockSize).value() - kBlockSize);
  memcpy(blocks, &header, sizeof(header));
  std::unique_ptr<GptDevice> gpt;
  EXPECT_OK(GptDevice::Load(blocks, MinimumBytesPerCopy(kBlockSize).value(), kBlockSize,
                            kBlockCount, &gpt));
  ASSERT_EQ(gpt->EntryCount(), entry_count);
}

TEST(GptDeviceMbr, LargerSectorSizes) {
  LibGptTest::Options options{};
  options.block_size = 4096;
  auto libGptTest = LibGptTest::Create(options);

  // Disk starts off uninitialized.
  EXPECT_FALSE(libGptTest->IsGptValid());

  // Sync should write changes to disk. Resetting should bring valid gpt back.
  libGptTest->Sync();
  libGptTest->Reset();
  EXPECT_TRUE(libGptTest->IsGptValid());

  // Check the protective MBR that was written to disk.
  mbr::Mbr mbr;
  zx_status_t status = libGptTest->ReadMbr(&mbr);
  ASSERT_OK(status, "Failed to read MBR");
  EXPECT_EQ(mbr::kMbrBootSignature, Unpack(mbr.boot_signature), "Invalid MBR boot signature");
}

TEST(GptDeviceMbr, DiskSize) {
  LibGptTest::Options options{};
  options.block_count = 0x10'0000;
  auto libGptTest = LibGptTest::Create(options);

  // Write changes to disk.
  libGptTest->Sync();
  EXPECT_TRUE(libGptTest->IsGptValid());

  // Check the protective MBR that was written to disk, and covers the size of the disk,
  // minus one (the MBR sector is not counted).
  mbr::Mbr mbr;
  zx_status_t status = libGptTest->ReadMbr(&mbr);
  ASSERT_OK(status, "Failed to read MBR");
  EXPECT_EQ(Unpack(mbr.partitions[0].num_sectors), 0x10'0000 - 1);
}

TEST(MakeProtectiveMbr, PartitionSize) {
  // Protective MBR should create a partition of size min(UINT32_MAX, num_sectors - 1).
  EXPECT_EQ(Unpack(MakeProtectiveMbr(100).partitions[0].num_sectors), 99);
  EXPECT_EQ(Unpack(MakeProtectiveMbr(0xffff'ffff).partitions[0].num_sectors), 0xffff'fffe);
  EXPECT_EQ(Unpack(MakeProtectiveMbr(0x10'abcd'1234).partitions[0].num_sectors), 0xffff'ffff);
}

// KnownGuid is statically built. Verify name uniqueness within each scheme.
TEST(KnownGuidTest, CheckNames) {
  for (auto i = KnownGuid::begin(); i != KnownGuid::end(); i++) {
    for (auto j = i + 1; j != KnownGuid::end(); j++) {
      // Old and new partition scheme sometimes share names, but in this case
      // the type GUID and scheme must be different.
      if (i->name() == j->name()) {
        EXPECT_NE(i->type_guid(), j->type_guid());
        EXPECT_NE(i->scheme(), j->scheme());
      }
    }
  }
}

// KnownGuid is statically built. Verify type GUID uniqueness except when
// shared by slotted partitions.
TEST(KnownGuidTest, CheckTypeGuids) {
  for (auto i = KnownGuid::begin(); i != KnownGuid::end(); i++) {
    for (auto j = i + 1; j != KnownGuid::end(); j++) {
      // Slotted partitions can share a type GUID.
      if (i->type_guid() == j->type_guid()) {
        // Names should differ only by the slot suffix character.
        EXPECT_EQ(i->name().substr(0, i->name().size() - 1),
                  j->name().substr(0, j->name().size() - 1));

        // Only the new partitioning scheme uses shared type GUIDs.
        EXPECT_EQ(i->scheme(), PartitionScheme::kNew);
        EXPECT_EQ(j->scheme(), PartitionScheme::kNew);
      }
    }
  }
}

TEST(KnownGuidTest, FindByTypeGuid) {
  std::list<const GuidProperties*> matches;

  // Legacy partition scheme.
  matches = KnownGuid::Find(std::nullopt, uuid::Uuid(GUID_INSTALL_VALUE), std::nullopt);
  EXPECT_EQ(matches.size(), 1);
  EXPECT_EQ(matches.front()->name(), "fuchsia-install");

  matches = KnownGuid::Find(std::nullopt, uuid::Uuid(GUID_BOOTLOADER_VALUE), std::nullopt);
  EXPECT_EQ(matches.size(), 1);
  EXPECT_EQ(matches.front()->name(), "bootloader");

  matches = KnownGuid::Find(std::nullopt, uuid::Uuid(GUID_ZIRCON_B_VALUE), std::nullopt);
  EXPECT_EQ(matches.size(), 1);
  EXPECT_EQ(matches.front()->name(), "zircon-b");

  // New patition scheme.
  matches = KnownGuid::Find(std::nullopt, uuid::Uuid(GPT_DURABLE_BOOT_TYPE_GUID), std::nullopt);
  EXPECT_EQ(matches.size(), 1);
  EXPECT_EQ(matches.front()->name(), "durable_boot");

  matches = KnownGuid::Find(std::nullopt, uuid::Uuid(GPT_FVM_TYPE_GUID), std::nullopt);
  EXPECT_EQ(matches.size(), 1);
  EXPECT_EQ(matches.front()->name(), "fvm");

  matches = KnownGuid::Find(std::nullopt, uuid::Uuid(GPT_BOOTLOADER_ABR_TYPE_GUID), std::nullopt);
  EXPECT_EQ(matches.size(), 3);
  EXPECT_EQ(matches.front()->name(), "bootloader_a");
  EXPECT_EQ((++matches.front())->name(), "bootloader_b");
  EXPECT_EQ(matches.back()->name(), "bootloader_r");

  // Unknown type GUID.
  matches = KnownGuid::Find(std::nullopt, TestGuid(0), std::nullopt);
  EXPECT_TRUE(matches.empty());
}

TEST(KnownGuidTest, FindByName) {
  std::list<const GuidProperties*> matches;

  // Legacy partition scheme.
  matches = KnownGuid::Find("fuchsia-system", std::nullopt, std::nullopt);
  EXPECT_EQ(matches.size(), 1);
  EXPECT_EQ(matches.front()->type_guid(), uuid::Uuid(GUID_SYSTEM_VALUE));

  matches = KnownGuid::Find("misc", std::nullopt, std::nullopt);
  EXPECT_EQ(matches.size(), 1);
  EXPECT_EQ(matches.front()->type_guid(), uuid::Uuid(GUID_ABR_META_VALUE));

  // vbmeta_{a,b,r} partitions exist in both legacy and new schemes.
  matches = KnownGuid::Find("vbmeta_a", std::nullopt, std::nullopt);
  EXPECT_EQ(matches.size(), 2);
  EXPECT_EQ(matches.front()->type_guid(), uuid::Uuid(GUID_VBMETA_A_VALUE));
  EXPECT_EQ(matches.front()->scheme(), PartitionScheme::kLegacy);
  EXPECT_EQ(matches.back()->type_guid(), uuid::Uuid(GPT_VBMETA_ABR_TYPE_GUID));
  EXPECT_EQ(matches.back()->scheme(), PartitionScheme::kNew);

  // New partition scheme.
  matches = KnownGuid::Find("durable", std::nullopt, std::nullopt);
  EXPECT_EQ(matches.size(), 1);
  EXPECT_EQ(matches.front()->type_guid(), uuid::Uuid(GPT_DURABLE_TYPE_GUID));

  // Unknown partition name.
  matches = KnownGuid::Find("unknown_name", std::nullopt, std::nullopt);
  EXPECT_TRUE(matches.empty());
}

TEST(KnownGuidTest, FindByPartitionScheme) {
  ASSERT_EQ(KnownGuid::Find(std::nullopt, std::nullopt, PartitionScheme::kLegacy).size(), 27);
  ASSERT_EQ(KnownGuid::Find(std::nullopt, std::nullopt, PartitionScheme::kNew).size(), 14);
}

TEST(KnownGuidTest, FindByAll) {
  std::list<const GuidProperties*> matches;

  // Legacy partition scheme.
  matches =
      KnownGuid::Find("fuchsia-system", uuid::Uuid(GUID_SYSTEM_VALUE), PartitionScheme::kLegacy);
  EXPECT_EQ(matches.size(), 1);
  EXPECT_EQ(matches.front()->name(), "fuchsia-system");
  EXPECT_EQ(matches.front()->type_guid(), uuid::Uuid(GUID_SYSTEM_VALUE));
  EXPECT_EQ(matches.front()->scheme(), PartitionScheme::kLegacy);

  // New partition scheme.
  matches = KnownGuid::Find("factory", uuid::Uuid(GPT_FACTORY_TYPE_GUID), PartitionScheme::kNew);
  EXPECT_EQ(matches.size(), 1);
  EXPECT_EQ(matches.front()->name(), "factory");
  EXPECT_EQ(matches.front()->type_guid(), uuid::Uuid(GPT_FACTORY_TYPE_GUID));
  EXPECT_EQ(matches.front()->scheme(), PartitionScheme::kNew);

  // Both schemes have a "factory" partition, but mix-and-match of the new type
  // GUID and old scheme should return nothing.
  matches = KnownGuid::Find("factory", uuid::Uuid(GPT_FACTORY_TYPE_GUID), PartitionScheme::kLegacy);
  EXPECT_TRUE(matches.empty());
}

// Type GUID string validation to make sure endianness is handled correctly.
TEST(KnownGuidTest, TypeGuidStrings) {
  std::list<const GuidProperties*> matches;

  // Legacy partition scheme.
  matches = KnownGuid::Find("cros-firmware", std::nullopt, std::nullopt);
  EXPECT_EQ(matches.size(), 1);
  EXPECT_EQ(HexToUpper(matches.front()->type_guid().ToString()),
            "CAB6E88E-ABF3-4102-A07A-D4BB9BE3C1D3");

  matches = KnownGuid::Find("fuchsia-fvm", std::nullopt, std::nullopt);
  EXPECT_EQ(matches.size(), 1);
  EXPECT_EQ(HexToUpper(matches.front()->type_guid().ToString()),
            "41D0E340-57E3-954E-8C1E-17ECAC44CFF5");

  // New partition scheme.
  matches = KnownGuid::Find("factory_boot", std::nullopt, std::nullopt);
  EXPECT_EQ(matches.size(), 1);
  EXPECT_EQ(HexToUpper(matches.front()->type_guid().ToString()),
            "10B8DBAA-D2BF-42A9-98C6-A7C5DB3701E7");

  matches = KnownGuid::Find("bootloader_r", std::nullopt, std::nullopt);
  EXPECT_EQ(matches.size(), 1);
  EXPECT_EQ(HexToUpper(matches.front()->type_guid().ToString()),
            "FE8A2634-5E2E-46BA-99E3-3A192091A350");
}

TEST(KnownGuidTest, TypeDescription) {
  // Legacy partition scheme.
  EXPECT_EQ(KnownGuid::TypeDescription(GUID_ZIRCON_A_VALUE), "zircon-a");
  EXPECT_EQ(KnownGuid::TypeDescription(GUID_FVM_VALUE), "fuchsia-fvm");

  // New partition scheme.
  EXPECT_EQ(KnownGuid::TypeDescription(GPT_FACTORY_BOOT_TYPE_GUID), "factory_boot");
  EXPECT_EQ(KnownGuid::TypeDescription(GPT_ZIRCON_ABR_TYPE_GUID), "zircon_*");

  // Unknown partition.
  EXPECT_EQ(KnownGuid::TypeDescription(TestGuid(0)), "");
}

// Make sure all known GUID entries have a valid TypeDescription.
TEST(KnownGuidTest, AllHaveTypeDescription) {
  for (auto i = KnownGuid::begin(); i != KnownGuid::end(); i++) {
    std::string description = KnownGuid::TypeDescription(i->type_guid());
    // Shouldn't be empty, and shouldn't just be "*" which would indicate
    // something went wrong with the prefix matcher.
    EXPECT_FALSE(description.empty());
    EXPECT_NE(description, "*");
  }
}

TEST(InitializePrimaryHeader, BlockSizeTooSmall) {
  ASSERT_EQ(InitializePrimaryHeader(sizeof(gpt_header_t) - 1, kBlockCount).error(),
            ZX_ERR_INVALID_ARGS);
}

TEST(InitializePrimaryHeader, BlockCountOne) {
  ASSERT_EQ(InitializePrimaryHeader(kBlockSize, 1).error(), ZX_ERR_BUFFER_TOO_SMALL);
}

TEST(InitializePrimaryHeader, BlockCountOneLessThanRequired) {
  uint64_t block_count = GptMinimumBlockCount(kBlockSize) - 1;
  ASSERT_EQ(InitializePrimaryHeader(kBlockSize, block_count).error(), ZX_ERR_BUFFER_TOO_SMALL);
}

TEST(InitializePrimaryHeader, BlockCountEqualsMinimumRequired) {
  uint64_t block_count = GptMinimumBlockCount(kBlockSize);
  ASSERT_TRUE(InitializePrimaryHeader(kBlockSize, block_count).is_ok());
}

TEST(InitializePrimaryHeader, CheckFields) {
  gpt_header_t header = InitializePrimaryHeader(kBlockSize, kBlockCount).value();

  ASSERT_EQ(header.magic, kMagicNumber);
  ASSERT_EQ(header.revision, kRevision);
  ASSERT_EQ(header.size, kHeaderSize);
  ASSERT_EQ(header.reserved0, 0);
  ASSERT_EQ(header.current, kPrimaryHeaderStartBlock);
  ASSERT_EQ(header.backup, kBlockCount - 1);
  ASSERT_EQ(header.first, kPrimaryHeaderStartBlock + 1 + EntryArrayBlockCount(kBlockSize));
  ASSERT_EQ(header.last, header.backup - EntryArrayBlockCount(kBlockSize) - 1);

  // Guid can be anything but all zeros
  ASSERT_NE(uuid::Uuid(header.guid), uuid::Uuid());
  ASSERT_EQ(header.entries, header.current + 1);
  ASSERT_EQ(header.entries_count, kPartitionCount);
  ASSERT_EQ(header.entries_size, kEntrySize);
  ASSERT_EQ(header.entries_crc, 0);

  uint32_t crc = header.crc32;
  header.crc32 = 0;
  ASSERT_EQ(crc, crc32(0, reinterpret_cast<uint8_t*>(&header), kHeaderSize));
}

TEST(ValidateHeaderTest, ValidHeader) {
  gpt_header_t header = InitializePrimaryHeader(kBlockSize, kBlockCount).value();
  ASSERT_EQ(ValidateHeader(&header, kBlockCount), ZX_OK);
}

TEST(ValidateHeaderTest, BadMagic) {
  gpt_header_t header = InitializePrimaryHeader(kBlockSize, kBlockCount).value();
  header.magic = ~header.magic;
  ASSERT_EQ(ValidateHeader(&header, kBlockCount), ZX_ERR_BAD_STATE);
}

TEST(ValidateHeaderTest, InvalidSize) {
  gpt_header_t header = InitializePrimaryHeader(kBlockSize, kBlockCount).value();
  header.size = header.size + 1;
  ASSERT_EQ(ValidateHeader(&header, kBlockCount), ZX_ERR_INVALID_ARGS);

  header.size = header.size - 2;
  ASSERT_EQ(ValidateHeader(&header, kBlockCount), ZX_ERR_INVALID_ARGS);
}

TEST(ValidateHeaderTest, BadCrc) {
  gpt_header_t header = InitializePrimaryHeader(kBlockSize, kBlockCount).value();
  header.crc32 = ~header.crc32;
  ASSERT_EQ(ValidateHeader(&header, kBlockCount), ZX_ERR_IO_DATA_INTEGRITY);
}

TEST(ValidateHeaderTest, TooManyPartitions) {
  gpt_header_t header = InitializePrimaryHeader(kBlockSize, kBlockCount).value();
  header.entries_count = kPartitionCount + 1;
  header.crc32 = 0;
  header.crc32 = crc32(0, reinterpret_cast<const uint8_t*>(&header), kHeaderSize);

  ASSERT_EQ(ValidateHeader(&header, kBlockCount), ZX_ERR_IO_OVERRUN);
}

TEST(ValidateHeaderTest, EntrySizeMismatch) {
  gpt_header_t header = InitializePrimaryHeader(kBlockSize, kBlockCount).value();
  header.entries_size = kEntrySize - 1;
  header.crc32 = 0;
  header.crc32 = crc32(0, reinterpret_cast<const uint8_t*>(&header), kHeaderSize);
  ASSERT_EQ(ValidateHeader(&header, kBlockCount), ZX_ERR_FILE_BIG);

  header.entries_size = kEntrySize + 1;
  header.crc32 = 0;
  header.crc32 = crc32(0, reinterpret_cast<const uint8_t*>(&header), kHeaderSize);
  ASSERT_EQ(ValidateHeader(&header, kBlockCount), ZX_ERR_FILE_BIG);
}

TEST(ValidateHeaderTest, BlockDeviceShrunk) {
  gpt_header_t header = InitializePrimaryHeader(kBlockSize, kBlockCount).value();

  ASSERT_EQ(ValidateHeader(&header, kBlockCount - 1), ZX_ERR_BUFFER_TOO_SMALL);
}

TEST(ValidateHeaderTest, FirstUsableBlockLargerThanLast) {
  gpt_header_t header = InitializePrimaryHeader(kBlockSize, kBlockCount).value();
  header.first = header.last + 1;
  header.crc32 = 0;
  header.crc32 = crc32(0, reinterpret_cast<const uint8_t*>(&header), kHeaderSize);

  ASSERT_EQ(ZX_ERR_OUT_OF_RANGE, ValidateHeader(&header, kBlockCount));
}

TEST(ValidateEntryTest, UninitializedEntry) {
  gpt_entry_t entry = {};
  ASSERT_FALSE(ValidateEntry(&entry).value());
}

TEST(ValidateEntryTest, ValidEntry) {
  gpt_entry_t entry = {};
  entry.guid[0] = 1;
  entry.type[0] = 1;
  entry.first = 10;
  entry.last = 20;
  ASSERT_TRUE(ValidateEntry(&entry).value());
}

TEST(ValidateEntryTest, UninitializedGuid) {
  gpt_entry_t entry = {};
  entry.guid[0] = 0;
  entry.type[0] = 1;
  entry.first = 10;
  entry.last = 20;
  ASSERT_EQ(ZX_ERR_BAD_STATE, ValidateEntry(&entry).error());
}

TEST(ValidateEntryTest, UninitializedType) {
  gpt_entry_t entry = {};
  entry.guid[0] = 1;
  entry.type[0] = 0;
  entry.first = 10;
  entry.last = 20;
  ASSERT_EQ(ZX_ERR_BAD_STATE, ValidateEntry(&entry).error());
}

TEST(ValidateEntryTest, BadRange) {
  gpt_entry_t entry = {};
  entry.guid[0] = 1;
  entry.type[0] = 1;
  entry.first = 20;
  entry.last = 10;
  ASSERT_EQ(ZX_ERR_OUT_OF_RANGE, ValidateEntry(&entry).error());
}

}  // namespace gpt
