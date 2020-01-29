// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "libgpt-tests.h"

#include <fuchsia/hardware/block/c/fidl.h>
#include <lib/fdio/cpp/caller.h>
#include <zircon/assert.h>

#include <memory>

#include <fbl/auto_call.h>
#include <ramdevice-client/ramdisk.h>
#include <unittest/unittest.h>

// generate a random number between [1, max]
uint64_t random_non_zero_length(uint64_t max) { return (rand() % max) + 1; }

extern bool gUseRamDisk;
extern char gDevPath[PATH_MAX];

namespace {

using gpt::GptDevice;

// TODO(auradkar): consolidate this guid_t definition with one in
// ulib/gpt/gpt.cpp.
struct guid_t {
  uint32_t data1;
  uint16_t data2;
  uint16_t data3;
  uint8_t data4[8];
};

constexpr guid_t kGuid = {0x0, 0x1, 0x2, {0x3, 0x4, 0x5, 0x6, 0x7, 0x8, 0x9, 0xa}};

constexpr uint64_t partition_size(const gpt_partition_t* p) { return p->last - p->first + 1; }

bool destroy_gpt(int fd, uint64_t block_size, uint64_t offset, uint64_t block_count) {
  BEGIN_HELPER;

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
  // TODO(ZX-3294) to fix this
  // ASSERT_EQ(fsync(fd), 0, "Failed to fsync");
  END_HELPER;
}

class Partitions {
 public:
  Partitions(uint32_t count, uint64_t first, uint64_t last);

  // Returns partition at index index. Returns null if index is out of range.
  const gpt_partition_t* GetPartition(uint32_t index) const;

  // Returns number of parition created/removed.
  uint32_t GetCount() const { return partition_count_; }

  // Marks a partition as created in GPT.
  bool MarkCreated(uint32_t index);

  // Mark a partition as removed in GPT.
  bool ClearCreated(uint32_t index);

  // Returns true if the GPT should have the partition.
  bool IsCreated(uint32_t index) const;

  // Returns number of partition that should exist on GPT.
  uint32_t CreatedCount() const;

  // Returns true if two partitions are the same.
  bool Compare(const gpt_partition_t* in_mem_partition,
               const gpt_partition_t* on_disk_partition) const;

  // Returns true if the partition p exists in partitions_.
  bool Find(const gpt_partition_t* p, uint32_t* out_index) const;

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
  guid_t guid = kGuid;

  uint64_t part_first = first, part_last;
  uint64_t part_max_len = (last - first) / partition_count_;
  ZX_ASSERT(part_max_len > 0);

  memset(partitions_, 0, sizeof(partitions_));
  for (uint32_t i = 0; i < partition_count_; i++) {
    part_last = part_first + random_non_zero_length(part_max_len);
    guid.data1 = i;
    memcpy(partitions_[i].type, &guid, sizeof(partitions_[i].type));
    memcpy(partitions_[i].guid, &guid, sizeof(partitions_[i].type));
    partitions_[i].first = part_first;
    partitions_[i].last = part_last;
    partitions_[i].flags = 0;
    memset(partitions_[i].name, 0, sizeof(partitions_[i].name));
    snprintf(reinterpret_cast<char*>(partitions_[i].name), sizeof(partitions_[i].name), "%u_part",
             i);

    part_first += part_max_len;
  }
}

const gpt_partition_t* Partitions::GetPartition(uint32_t index) const {
  if (index >= partition_count_) {
    return nullptr;
  }

  return &partitions_[index];
}

bool Partitions::MarkCreated(uint32_t index) {
  BEGIN_HELPER;
  ASSERT_LT(index, partition_count_, "Index out of range");

  created_[index] = true;
  END_HELPER;
}

bool Partitions::ClearCreated(uint32_t index) {
  BEGIN_HELPER;
  ASSERT_LT(index, partition_count_, "Index out of range");

  created_[index] = false;
  END_HELPER;
}

bool Partitions::IsCreated(uint32_t index) const {
  BEGIN_HELPER;
  ASSERT_LT(index, partition_count_, "Index out of range");

  return created_[index];
  END_HELPER;
}

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

// Defines a libgpt test function which can be passed to the TestWrapper.
typedef bool (*TestFunction)(LibGptTest* libGptTest);
#define RUN_TEST_WRAP(test_name) RUN_TEST_MEDIUM(TestWrapper<test_name>)

// A test wrapper which runs a libgpt test.
template <TestFunction TestFunc>
bool TestWrapper(void) {
  BEGIN_TEST;

  LibGptTest libGptTest(gUseRamDisk);
  ASSERT_TRUE(libGptTest.Init(), "Setting up the block device");
  // Run the test. This should pass.
  ASSERT_TRUE(TestFunc(&libGptTest));
  ASSERT_TRUE(libGptTest.Teardown(), "Tearing down and cleaning up the block device");

  END_TEST;
}

bool LibGptTest::Reset() {
  BEGIN_HELPER;
  std::unique_ptr<GptDevice> gpt;

  // explicitly close the fd, if open, before we attempt to reopen it.
  fd_.reset();

  fd_.reset(open(disk_path_, O_RDWR));

  ASSERT_TRUE(fd_.is_valid(), "Could not open block device\n");
  ASSERT_EQ(GptDevice::Create(fd_.get(), GetBlockSize(), GetBlockCount(), &gpt), ZX_OK);
  gpt_ = std::move(gpt);
  END_HELPER;
}

bool LibGptTest::Finalize() {
  BEGIN_HELPER;
  ASSERT_FALSE(gpt_->Valid(), "Valid GPT on uninitialized disk");

  ASSERT_EQ(gpt_->Finalize(), ZX_OK, "Failed to finalize");
  ASSERT_TRUE(gpt_->Valid(), "Invalid GPT after finalize");
  END_HELPER;
}

bool LibGptTest::Sync() {
  BEGIN_HELPER;

  ASSERT_EQ(gpt_->Sync(), ZX_OK, "Failed to sync");
  ASSERT_TRUE(gpt_->Valid(), "Invalid GPT after sync");

  END_HELPER;
}

bool LibGptTest::ReadRange() {
  BEGIN_HELPER;

  ASSERT_EQ(gpt_->Range(&usable_start_block_, &usable_last_block_), ZX_OK,
            "Retrieval of device range failed.");

  // TODO(auradkar): GptDevice doesn't export api to get GPT-metadata size.
  // If it does, we can keep better track of metadata size it says it needs
  // and metadata it actually uses.
  ASSERT_LT(GetUsableStartBlock(), GetBlockCount(), "Range starts after EOD");
  ASSERT_LT(GetUsableStartBlock(), GetUsableLastBlock(), "Invalid range");
  ASSERT_LT(GetUsableLastBlock(), GetBlockCount(), "Range end greater than block count");
  ASSERT_GT(GetUsableBlockCount(), 0, "GPT occupied all available blocks");

  END_HELPER;
}

bool LibGptTest::PrepDisk(bool sync) {
  BEGIN_HELPER;

  if (sync) {
    Sync();
  } else {
    Finalize();
  }

  ASSERT_TRUE(ReadRange(), "Read range failed");
  END_HELPER;
}

bool LibGptTest::InitDisk(const char* disk_path) {
  BEGIN_HELPER;

  use_ramdisk_ = false;
  snprintf(disk_path_, PATH_MAX, "%s", disk_path);
  fbl::unique_fd fd(open(disk_path_, O_RDWR));
  ASSERT_TRUE(fd.is_valid(), "Could not open block device to fetch info");
  fuchsia_hardware_block_BlockInfo block_info;
  fdio_cpp::UnownedFdioCaller caller(fd.get());
  zx_status_t status;
  ASSERT_EQ(fuchsia_hardware_block_BlockGetInfo(caller.borrow_channel(), &status, &block_info),
            ZX_OK);
  ASSERT_EQ(status, ZX_OK);

  blk_size_ = block_info.block_size;
  blk_count_ = block_info.block_count;

  ASSERT_GE(GetDiskSize(), kAccptableMinimumSize, "Insufficient disk space for tests");
  fd_ = std::move(fd);

  END_HELPER;
}

bool LibGptTest::InitRamDisk() {
  BEGIN_HELPER;
  ASSERT_EQ(ramdisk_create(GetBlockSize(), GetBlockCount(), &ramdisk_), ZX_OK,
            "Could not create ramdisk");
  strlcpy(disk_path_, ramdisk_get_path(ramdisk_), sizeof(disk_path_));
  fd_.reset(open(disk_path_, O_RDWR));
  if (!fd_) {
    return false;
  }

  END_HELPER;
}

bool LibGptTest::Init() {
  BEGIN_HELPER;
  auto error = fbl::MakeAutoCall([this]() { Teardown(); });
  if (use_ramdisk_) {
    ASSERT_TRUE(InitRamDisk());
  } else {
    ASSERT_TRUE(InitDisk(gDevPath));
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
  ASSERT_TRUE(destroy_gpt(fd_.get(), GetBlockSize(), 0, GptMetadataBlocksCount()),
              "Failed to destroy gpt");

  ASSERT_TRUE(Reset());
  error.cancel();
  END_HELPER;
}

bool LibGptTest::TearDownDisk() {
  BEGIN_HELPER;
  ASSERT_FALSE(use_ramdisk_);
  END_HELPER;
}

bool LibGptTest::TearDownRamDisk() {
  BEGIN_HELPER;
  ASSERT_EQ(ramdisk_destroy(ramdisk_), ZX_OK);
  END_HELPER;
}

bool LibGptTest::Teardown() {
  BEGIN_HELPER;

  if (use_ramdisk_) {
    ASSERT_TRUE(TearDownRamDisk());
  } else {
    ASSERT_TRUE(TearDownDisk());
  }

  END_HELPER;
}

}  // namespace

// Creates "partitions->GetCount()"" number of partitions on GPT.
//  The information needed to create a partitions is passed in "partitions".
bool AddPartitionHelper(LibGptTest* libGptTest, Partitions* partitions) {
  BEGIN_HELPER;

  ASSERT_GT(partitions->GetCount(), 0, "At least one partition is required");
  for (uint32_t i = 0; i < partitions->GetCount(); i++) {
    const gpt_partition_t* p = partitions->GetPartition(i);
    ASSERT_EQ(libGptTest->AddPartition(reinterpret_cast<const char*>(p->name), p->type, p->guid,
                                       p->first, partition_size(p), p->flags),
              ZX_OK, "Add partition failed");
    partitions->MarkCreated(i);
  }

  END_HELPER;
}

// Removes randomly selected "remove_count" number of partitions.
bool RemovePartitionsHelper(LibGptTest* libGptTest, Partitions* partitions, uint32_t remove_count) {
  BEGIN_HELPER;
  uint32_t index;

  ASSERT_LE(remove_count, partitions->GetCount(), "Remove count exceeds whats available");
  ASSERT_LE(remove_count, partitions->CreatedCount(), "Cannot remove more partitions than created");

  for (uint32_t i = 0; i < remove_count; i++) {
    while (true) {
      index = static_cast<uint32_t>(rand()) % partitions->GetCount();
      if (partitions->IsCreated(index)) {
        break;
      }
    }
    ASSERT_TRUE(partitions->IsCreated(index), "Partition already removed");
    const gpt_partition_t* p = partitions->GetPartition(index);
    ASSERT_EQ(libGptTest->RemovePartition(p->guid), ZX_OK, "Failed to remove partition");
    partitions->ClearCreated(index);
  }
  END_HELPER;
}

// Verifies all the partitions that exists on GPT are the ones that are created
// by the test and vice-versa.
bool PartitionVerify(LibGptTest* libGptTest, const Partitions* partitions) {
  BEGIN_HELPER;
  bool found[gpt::kPartitionCount] = {};
  uint32_t found_index;

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

  END_TEST;
}

// Creates partitions and verifies them.
bool AddPartitions(LibGptTest* libGptTest, Partitions* partitions, bool sync) {
  BEGIN_HELPER;

  ASSERT_TRUE(AddPartitionHelper(libGptTest, partitions), "AddPartitionHelper failed");

  if (sync) {
    ASSERT_TRUE(libGptTest->Sync(), "Sync failed");
  }

  ASSERT_TRUE(PartitionVerify(libGptTest, partitions), "Partition verify failed");
  ASSERT_EQ(partitions->GetCount(), partitions->CreatedCount(),
            "Not as many created as we wanted to");

  END_HELPER;
}

// Removes partitions and verifies them.
bool RemovePartitions(LibGptTest* libGptTest, Partitions* partitions, uint32_t remove_count,
                      bool sync) {
  BEGIN_HELPER;

  ASSERT_TRUE(RemovePartitionsHelper(libGptTest, partitions, remove_count),
              "RemovePartitionsHelper failed");
  if (sync) {
    ASSERT_TRUE(libGptTest->Sync(), "Sync failed");
  }

  ASSERT_TRUE(PartitionVerify(libGptTest, partitions), "Partition verify failed");
  ASSERT_EQ(partitions->GetCount() - partitions->CreatedCount(), remove_count,
            "Not as many removed as we wanted to");

  END_HELPER;
}

// Removes all partitions and verifies them.
bool RemoveAllPartitions(LibGptTest* libGptTest, Partitions* partitions, bool sync) {
  BEGIN_HELPER;

  ASSERT_LE(partitions->GetCount(), partitions->CreatedCount(), "Not all partitions populated");
  ASSERT_EQ(libGptTest->RemoveAllPartitions(), ZX_OK, "Failed to remove all partition");

  for (uint32_t i = 0; i < partitions->GetCount(); i++) {
    partitions->ClearCreated(i);
  }

  ASSERT_TRUE(PartitionVerify(libGptTest, partitions), "Partition verify failed");
  ASSERT_EQ(partitions->CreatedCount(), 0, "Not as many removed as we wanted to");
  END_HELPER;
}

// Tests if we can create a GptDevice.
bool CreateTest(LibGptTest* libGptTest) {
  BEGIN_TEST;

  ASSERT_FALSE(libGptTest->IsGptValid(), "Valid GPT on uninitialized disk");
  ASSERT_TRUE(libGptTest->Reset(), "Failed to reset Test");
  ASSERT_FALSE(libGptTest->IsGptValid(), "Valid GPT after reset");
  END_TEST;
}

// Tests Finalize initializes GPT in-memory only and doesn't commit to disk.
bool FinalizeTest(LibGptTest* libGptTest) {
  BEGIN_TEST;

  ASSERT_TRUE(libGptTest->Finalize(), "Finalize failed");

  // Finalize initializes GPT but doesn't write changes to disk.
  // Resetting the Test should bring invalid gpt back.
  ASSERT_TRUE(libGptTest->Reset(), "Failed to reset Test");
  ASSERT_FALSE(libGptTest->IsGptValid(), "Valid GPT after finalize and reset");
  END_TEST;
}

// Tests Finalize initializes GPT and writes it to disk.
bool SyncTest(LibGptTest* libGptTest) {
  BEGIN_TEST;

  ASSERT_FALSE(libGptTest->IsGptValid(), "Valid GPT on uninitialized disk");

  // Sync should write changes to disk. Resetting should bring valid gpt back.
  ASSERT_TRUE(libGptTest->Sync(), "Sync failed");
  ASSERT_TRUE(libGptTest->Reset(), "Failed to reset Test");
  ASSERT_TRUE(libGptTest->IsGptValid(), "Invalid GPT after sync and reset");
  END_TEST;
}

// Tests the range the GPT blocks falls within disk.
bool RangeTest(LibGptTest* libGptTest) {
  BEGIN_TEST;

  ASSERT_TRUE(libGptTest->Finalize(), "Finalize failed");
  ASSERT_TRUE(libGptTest->ReadRange(), "Failed to read range");

  END_TEST;
}

// Test adding total_partitions partitions to GPT w/o writing to disk
template <uint32_t total_partitions, bool sync>
bool AddPartitionTest(LibGptTest* libGptTest) {
  BEGIN_TEST;
  ASSERT_TRUE(libGptTest->PrepDisk(sync), "Failed to setup disk");

  Partitions partitions(total_partitions, libGptTest->GetUsableStartBlock(),
                        libGptTest->GetUsableLastBlock());

  ASSERT_TRUE(AddPartitions(libGptTest, &partitions, sync), "AddPartitions failed");
  END_TEST;
}

// Test adding total_partitions partitions to GPT and test removing remove_count
// partitions later w/o writing to disk.
template <uint32_t total_partitions, uint32_t remove_count, bool sync>
bool RemovePartitionTest(LibGptTest* libGptTest) {
  BEGIN_TEST;
  ASSERT_TRUE(libGptTest->PrepDisk(sync), "Failed to setup disk");

  Partitions partitions(total_partitions, libGptTest->GetUsableStartBlock(),
                        libGptTest->GetUsableLastBlock());

  ASSERT_TRUE(AddPartitions(libGptTest, &partitions, sync), "AddPartitions failed");
  ASSERT_TRUE(RemovePartitions(libGptTest, &partitions, remove_count, sync),
              "RemovePartitions failed");

  END_TEST;
}

// Test removing all total_partititions from GPT w/o syncing.
template <uint32_t total_partitions, bool sync>
bool RemovePartitionAllTest(LibGptTest* libGptTest) {
  BEGIN_TEST;

  ASSERT_TRUE(libGptTest->PrepDisk(sync), "Failed to setup disk");

  Partitions partitions(total_partitions, libGptTest->GetUsableStartBlock(),
                        libGptTest->GetUsableLastBlock());

  ASSERT_TRUE(AddPartitions(libGptTest, &partitions, sync), "AddPartitions failed");
  ASSERT_TRUE(RemoveAllPartitions(libGptTest, &partitions, sync), "RemoveAllPartitions failed");

  END_TEST;
}

// Test if Diffs after adding partitions reflect all the changes.
template <uint32_t total_partitions>
bool DiffsTest(LibGptTest* libGptTest) {
  BEGIN_TEST;
  uint32_t diffs;

  ASSERT_NE(libGptTest->GetDiffs(0, &diffs), ZX_OK, "GetDiffs should fail before PrepDisk");
  ASSERT_TRUE(libGptTest->PrepDisk(false), "Failed to setup disk");
  ASSERT_NE(libGptTest->GetDiffs(0, &diffs), ZX_OK,
            "GetDiffs for non-existing partition should fail");

  Partitions partitions(total_partitions, libGptTest->GetUsableStartBlock(),
                        libGptTest->GetUsableLastBlock());
  ASSERT_TRUE(AddPartitions(libGptTest, &partitions, false), "AddPartitions failed");
  ASSERT_EQ(libGptTest->GetDiffs(0, &diffs), ZX_OK, "Diffs zero after adding partition");

  ASSERT_EQ(diffs,
            gpt::kGptDiffType | gpt::kGptDiffGuid | gpt::kGptDiffFirst | gpt::kGptDiffLast |
                gpt::kGptDiffName,
            "Unexpected diff after creating partition");
  ASSERT_TRUE(libGptTest->Sync(), "Failed to sync");
  ASSERT_EQ(libGptTest->GetDiffs(0, &diffs), ZX_OK, "GetDiffs failed");
  ASSERT_EQ(diffs, 0, "Diffs not zero after syncing partition");

  END_TEST;
}

BEGIN_TEST_CASE(libgpt_tests)
RUN_TEST_WRAP(CreateTest)
RUN_TEST_WRAP(FinalizeTest)
RUN_TEST_WRAP(SyncTest)
RUN_TEST_WRAP(RangeTest)
RUN_TEST_WRAP((AddPartitionTest<3, false>))
RUN_TEST_WRAP((AddPartitionTest<20, true>))
RUN_TEST_WRAP((RemovePartitionTest<12, 4, false>))
RUN_TEST_WRAP((RemovePartitionTest<3, 2, true>))
RUN_TEST_WRAP((RemovePartitionTest<11, 11, false>))
RUN_TEST_WRAP((RemovePartitionAllTest<12, true>))
RUN_TEST_WRAP((RemovePartitionAllTest<15, false>))
RUN_TEST_WRAP((DiffsTest<9>))
END_TEST_CASE(libgpt_tests);
