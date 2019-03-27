// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fbl/auto_call.h>
#include <fuchsia/hardware/block/c/fidl.h>
#include <lib/fzl/fdio.h>
#include <ramdevice-client/ramdisk.h>
#include <unittest/unittest.h>
#include <zircon/assert.h>

#include "gpt-tests.h"
#include <gpt/guid.h>

extern bool gUseRamDisk;
extern char gDevPath[PATH_MAX];
extern unsigned int gRandSeed;

namespace {

using gpt::GptDevice;
using gpt::guid_t;
using gpt::KnownGuid;

constexpr guid_t kGuid = {0x0, 0x1, 0x2, {0x3, 0x4, 0x5, 0x6, 0x7, 0x8, 0x9, 0xa}};
constexpr uint64_t kHoleSize = 10;

// generate a random number between [0, max)
uint64_t random_length(uint64_t max) {
    return (rand_r(&gRandSeed) % max);
}

constexpr uint64_t partition_size(const gpt_partition_t* p) {
    return p->last - p->first + 1;
}

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

    // Changes gpt_partition_t.type. One of the fields in the guid_t
    // is increamented.
    bool ChangePartitionType(uint32_t partition_index);

    // Changes gpt_partition_t.guid. One of the fields in the guid_t
    // is increamented.
    bool ChangePartitionGuid(uint32_t partition_index);

    // Sets the visibility attribute of the partition
    bool SetPartitionVisibility(uint32_t partition_index, bool visible);

    // Changes the range a partition covers. The function doesn't check if these
    // changes are valid or not (whether they over lap with other partitions,
    // cross device limits)
    bool ChangePartitionRange(uint32_t partition_index, uint64_t start, uint64_t end);

    // Gets the current value of gpt_partition_t.flags
    bool GetPartitionFlags(uint32_t partition_index, uint64_t* flags) const;

    // Sets the current value of gpt_partition_t.flags
    bool SetPartitionFlags(uint32_t partition_index, uint64_t flags);

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
        part_last = part_first + random_length(part_max_len);
        guid.data1 = i;
        memcpy(partitions_[i].type, &guid, sizeof(partitions_[i].type));
        memcpy(partitions_[i].guid, &guid, sizeof(partitions_[i].type));
        partitions_[i].first = part_first;
        partitions_[i].last = part_last;
        partitions_[i].flags = 0;
        memset(partitions_[i].name, 0, sizeof(partitions_[i].name));
        snprintf(reinterpret_cast<char*>(partitions_[i].name), sizeof(partitions_[i].name),
                 "%u_part", i);

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

    if (strncmp(name, reinterpret_cast<const char*>(in_mem_partition->name), GPT_NAME_LEN / 2) !=
        0) {
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

void IncrementGuid(guid_t* g) {
    g->data3++;
}

bool Partitions::ChangePartitionGuid(uint32_t partition_index) {
    BEGIN_HELPER;
    ASSERT_LT(partition_index, partition_count_, "Partition index out of range");
    IncrementGuid(reinterpret_cast<guid_t*>(partitions_[partition_index].guid));
    END_HELPER;
}

bool Partitions::ChangePartitionType(uint32_t partition_index) {
    BEGIN_HELPER;
    ASSERT_LT(partition_index, partition_count_, "Partition index out of range");
    IncrementGuid(reinterpret_cast<guid_t*>(partitions_[partition_index].type));
    END_HELPER;
}

bool Partitions::SetPartitionVisibility(uint32_t partition_index, bool visible) {
    BEGIN_HELPER;
    ASSERT_LT(partition_index, partition_count_, "Partition index out of range");
    gpt::SetPartitionVisibility(&partitions_[partition_index], visible);
    END_HELPER;
}

bool Partitions::ChangePartitionRange(uint32_t partition_index, uint64_t start, uint64_t end) {
    BEGIN_HELPER;
    ASSERT_LT(partition_index, partition_count_, "Partition index out of range");
    partitions_[partition_index].first = start;
    partitions_[partition_index].last = end;
    END_HELPER;
}

bool Partitions::GetPartitionFlags(uint32_t partition_index, uint64_t* flags) const {
    BEGIN_HELPER;
    ASSERT_LT(partition_index, partition_count_, "Partition index out of range");
    *flags = partitions_[partition_index].flags;
    END_HELPER;
}

bool Partitions::SetPartitionFlags(uint32_t partition_index, uint64_t flags) {
    BEGIN_HELPER;
    ASSERT_LT(partition_index, partition_count_, "Partition index out of range");
    partitions_[partition_index].flags = flags;
    END_HELPER;
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
    fbl::unique_ptr<GptDevice> gpt;

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
    fzl::UnownedFdioCaller disk_caller(fd.get());
    fuchsia_hardware_block_BlockInfo block_info;
    zx_status_t status;
    ASSERT_EQ(fuchsia_hardware_block_BlockGetInfo(disk_caller.borrow_channel(), &status,
                                                  &block_info),
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

} // namespace

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
    ASSERT_LE(remove_count, partitions->CreatedCount(),
              "Cannot remove more partitions than created");

    for (uint32_t i = 0; i < remove_count; i++) {
        while (true) {
            index = static_cast<uint32_t>(rand_r(&gRandSeed)) % partitions->GetCount();
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

        ASSERT_TRUE(partitions->Find(p, &found_index),
                    "Found an entry on GPT that we did not create");

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

template <uint32_t total_partitions, bool sync>
bool SetPartitionTypeTest(LibGptTest* libGptTest) {
    BEGIN_TEST;
    guid_t before, after;

    ASSERT_TRUE(libGptTest->PrepDisk(sync), "Failed to setup disk");

    Partitions partitions(total_partitions, libGptTest->GetUsableStartBlock(),
                          libGptTest->GetUsableLastBlock());

    ASSERT_TRUE(AddPartitions(libGptTest, &partitions, sync), "AddPartitions failed");

    // Change partition type in cached copy so that we can verify the
    // changes in GptDevice
    uint32_t index = static_cast<uint32_t>(rand_r(&gRandSeed) % total_partitions);
    partitions.ChangePartitionType(index);

    // Keep a backup copy of GptDevice's partition type
    const gpt_partition_t* p = libGptTest->GetPartition(index);
    memcpy(&before, p->type, sizeof(before));

    // Change the type in GptDevice
    ASSERT_EQ(libGptTest->SetPartitionType(index, partitions.GetPartition(index)->type), ZX_OK, "");

    // Get the changes
    p = libGptTest->GetPartition(index);
    memcpy(&after, p->type, sizeof(after));

    // The type should have changed by now in GptDevice
    ASSERT_NE(memcmp(&before, &after, sizeof(before)), 0, "Same type before and after");

    ASSERT_TRUE(PartitionVerify(libGptTest, &partitions), "");
    END_TEST;
}

template <uint32_t total_partitions, bool sync>
bool SetPartitionGuidTest(LibGptTest* libGptTest) {
    BEGIN_TEST;
    guid_t before, after;

    ASSERT_TRUE(libGptTest->PrepDisk(sync), "Failed to setup disk");

    Partitions partitions(total_partitions, libGptTest->GetUsableStartBlock(),
                          libGptTest->GetUsableLastBlock());

    ASSERT_TRUE(AddPartitions(libGptTest, &partitions, sync), "AddPartitions failed");

    // Change partition id in cached copy so that we can verify the
    // changes in GptDevice
    uint32_t index = static_cast<uint32_t>(rand_r(&gRandSeed) % total_partitions);
    partitions.ChangePartitionGuid(index);

    // Keep a backup copy of GptDevice's partition ID
    const gpt_partition_t* p = libGptTest->GetPartition(index);
    memcpy(&before, p->guid, sizeof(before));

    // Change the guid in GptDevice
    ASSERT_EQ(libGptTest->SetPartitionGuid(index, partitions.GetPartition(index)->guid), ZX_OK, "");

    // Get the changes
    p = libGptTest->GetPartition(index);
    memcpy(&after, p->guid, sizeof(after));

    // The guid should have changed by now in GptDevice
    ASSERT_NE(memcmp(&before, &after, sizeof(before)), 0, "Same guid before and after");

    ASSERT_TRUE(PartitionVerify(libGptTest, &partitions), "");
    END_TEST;
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
        if ((partitions->GetPartition(index + 1)->first - partitions->GetPartition(index)->last) >
            1) {
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

template <uint32_t total_partitions, bool sync, find_partition_t find_part>
bool SetPartitionRangeTest(LibGptTest* libGptTest) {
    BEGIN_TEST;
    uint64_t new_last = 0, new_first = 0;

    ASSERT_GT(total_partitions, 1, "For range to test we need at least two partition");

    ASSERT_TRUE(libGptTest->PrepDisk(sync), "Failed to setup disk");

    Partitions partitions(total_partitions, libGptTest->GetUsableStartBlock(),
                          libGptTest->GetUsableLastBlock() - kHoleSize);

    ASSERT_TRUE(AddPartitions(libGptTest, &partitions, sync), "AddPartitions failed");

    uint32_t index;
    ASSERT_EQ(find_part(&partitions, &index, &new_first, &new_last), ZX_OK, "");

    ASSERT_NE(index, partitions.GetCount(), "Could not find a hole");
    ASSERT_NE(new_first, 0, "Could not find a hole to change range");
    ASSERT_NE(new_last, 0, "Could not find a hole to change range");

    ASSERT_TRUE(partitions.ChangePartitionRange(index, new_first, new_last), "");

    // Change the range in GptDevice
    ASSERT_EQ(libGptTest->SetPartitionRange(index, new_first, new_last), ZX_OK, "");

    // Get the changes
    gpt_partition_t* p = libGptTest->GetPartition(index);
    ASSERT_EQ(p->first, new_first, "First doesn't match after update");
    ASSERT_EQ(p->last, new_last, "Last doesn't match after update");

    ASSERT_TRUE(PartitionVerify(libGptTest, &partitions), "Partition verify failed");
    END_TEST;
}

bool PartitionVisibilityFlip(LibGptTest* libGptTest, Partitions* partitions, uint32_t index) {
    BEGIN_HELPER;

    // Get the current visibility and flip it
    const gpt_partition_t* p = libGptTest->GetPartition(index);
    bool visible = gpt::IsPartitionVisible(p);
    visible = !visible;
    partitions->SetPartitionVisibility(index, visible);

    // Change the guid in GptDevice
    ASSERT_EQ(libGptTest->SetPartitionVisibility(index, visible), ZX_OK, "");

    // Get the changes and verify
    p = libGptTest->GetPartition(index);
    ASSERT_EQ(gpt::IsPartitionVisible(p), visible, "Changes not reflected");
    ASSERT_TRUE(PartitionVerify(libGptTest, partitions), "");

    END_HELPER;
}

template <uint32_t total_partitions, bool sync>
bool PartitionVisibilityTest(LibGptTest* libGptTest) {
    BEGIN_TEST;

    ASSERT_TRUE(libGptTest->PrepDisk(sync), "Failed to setup disk");

    Partitions partitions(total_partitions, libGptTest->GetUsableStartBlock(),
                          libGptTest->GetUsableLastBlock());

    ASSERT_TRUE(AddPartitions(libGptTest, &partitions, sync), "AddPartitions failed");
    uint32_t index = static_cast<uint32_t>(rand_r(&gRandSeed) % total_partitions);

    ASSERT_TRUE(PartitionVisibilityFlip(libGptTest, &partitions, index), "");
    ASSERT_TRUE(PartitionVisibilityFlip(libGptTest, &partitions, index), "Flipping visibility");
    END_TEST;
}

bool PartitionFlagsFlip(LibGptTest* libGptTest, Partitions* partitions, uint32_t index) {
    BEGIN_HELPER;

    // Get the current flags
    uint64_t old_flags, updated_flags;
    ASSERT_EQ(libGptTest->GetPartitionFlags(index, &old_flags), ZX_OK, "");

    uint64_t new_flags = ~old_flags;
    partitions->SetPartitionFlags(index, new_flags);

    // Change the flags
    ASSERT_EQ(libGptTest->SetPartitionFlags(index, new_flags), ZX_OK, "");

    // Get the changes and verify
    ASSERT_EQ(libGptTest->GetPartitionFlags(index, &updated_flags), ZX_OK, "");
    ASSERT_EQ(new_flags, updated_flags, "Flags update failed");
    ASSERT_TRUE(PartitionVerify(libGptTest, partitions), "");

    END_HELPER;
}

template <uint32_t total_partitions, bool sync>
bool PartitionFlagsTest(LibGptTest* libGptTest) {
    BEGIN_TEST;

    ASSERT_TRUE(libGptTest->PrepDisk(sync), "Failed to setup disk");

    Partitions partitions(total_partitions, libGptTest->GetUsableStartBlock(),
                          libGptTest->GetUsableLastBlock());

    ASSERT_TRUE(AddPartitions(libGptTest, &partitions, sync), "AddPartitions failed");
    uint32_t index = static_cast<uint32_t>(rand_r(&gRandSeed) % total_partitions);

    ASSERT_TRUE(PartitionFlagsFlip(libGptTest, &partitions, index), "");
    ASSERT_TRUE(PartitionFlagsFlip(libGptTest, &partitions, index), "Flipping flags");
    END_TEST;
}

// Test if Diffs after adding partitions reflect all the changes.
template <uint32_t total_partitions>
bool DiffsTest(LibGptTest* libGptTest) {
    BEGIN_TEST;
    uint32_t diffs;

    ASSERT_NE(libGptTest->GetDiffs(0, &diffs), ZX_OK, "GetDiffs should fail before PrepDisk");
    ASSERT_TRUE(libGptTest->PrepDisk(false), "");
    ASSERT_NE(libGptTest->GetDiffs(0, &diffs), ZX_OK,
              "GetDiffs for non-existing partition should fail");

    Partitions partitions(total_partitions, libGptTest->GetUsableStartBlock(),
                          libGptTest->GetUsableLastBlock());
    ASSERT_TRUE(AddPartitions(libGptTest, &partitions, false), "");
    ASSERT_EQ(libGptTest->GetDiffs(0, &diffs), ZX_OK, "Diffs zero after adding partition");

    ASSERT_EQ(diffs,
              gpt::kGptDiffType | gpt::kGptDiffGuid | gpt::kGptDiffFirst | gpt::kGptDiffLast |
                  gpt::kGptDiffName,
              "Unexpected diff after creating partition");
    ASSERT_TRUE(libGptTest->Sync(), "");
    ASSERT_EQ(libGptTest->GetDiffs(0, &diffs), ZX_OK, "");
    ASSERT_EQ(diffs, 0, "Diffs not zero after syncing partition");

    END_TEST;
}

// KnownGuid is statically built. Verify that there are no double entries for
// human friendly GUID name.
bool KnownGuidUniqueNameTest() {
    BEGIN_TEST;
    for (auto i = KnownGuid::begin(); i != KnownGuid::end(); i++) {
        for (auto j = i + 1; j != KnownGuid::end(); j++) {
            ASSERT_NE(strcmp(i->name(), j->name()), 0, "Guid names not unique");
        }
    }
    END_TEST;
}

// KnownGuid is statically built. Verify that there are no double entries for
// GUID.
bool KnownGuidUniqueGuidTest() {
    BEGIN_TEST;
    for (auto i = KnownGuid::begin(); i != KnownGuid::end(); i++) {
        for (auto j = i + 1; j != KnownGuid::end(); j++) {
            ASSERT_NE(memcmp(i->guid(), j->guid(), sizeof(guid_t)), 0, "Guid not unique");
        }
    }
    END_TEST;
}

// KnownGuid is statically built. Verify that there are no double entries for
// human friendly GUID string.
bool KnownGuidUniqueStrTest() {
    BEGIN_TEST;
    for (auto i = KnownGuid::begin(); i != KnownGuid::end(); i++) {
        for (auto j = i + 1; j != KnownGuid::end(); j++) {
            ASSERT_NE(strcmp(i->str(), j->str()), 0, "Guid str not unique");
        }
    }
    END_TEST;
}

// KnownGuid is statically built. Verify that there are no wrong entries for GUID to
// string conversion.
bool KnownGuidToStrTest() {
    BEGIN_TEST;
    char str[GPT_NAME_LEN];
    bool pass = true;
    for (auto i = KnownGuid::begin(); i != KnownGuid::end(); i++) {
        uint8_to_guid_string(str, i->guid());
        if (strcmp(i->str(), str) != 0) {
            printf("for %s: %s and %s don't match\n", i->name(), i->str(), str);
            pass = false;
        }
    }

    ASSERT_TRUE(pass, "test failed");
    END_TEST;
}

// Litmus test for Guid to human friendly Name conversions
bool GuidToNameTest() {
    BEGIN_TEST;

    uint8_t install[GPT_GUID_LEN] = GUID_INSTALL_VALUE;
    auto res = KnownGuid::GuidToName(install);
    ASSERT_EQ(strcmp(res, "fuchsia-install"), 0, "Could not lookup fuchsia-install");

    uint8_t bl[GPT_GUID_LEN] = GUID_BOOTLOADER_VALUE;
    res = KnownGuid::GuidToName(bl);
    ASSERT_EQ(strcmp(res, "bootloader"), 0, "Could not lookup bootloader");

    uint8_t zb[GPT_GUID_LEN] = GUID_ZIRCON_B_VALUE;
    res = KnownGuid::GuidToName(zb);
    ASSERT_EQ(strcmp(res, "zircon-b"), 0, "Could not lookup zircon-b");

    END_TEST;
}

// Litmus test for Guid to human friendly Name conversions
bool NameToGuidTest() {
    BEGIN_TEST;
    uint8_t guid[GPT_GUID_LEN];

    uint8_t sys[GPT_GUID_LEN] = GUID_SYSTEM_VALUE;
    ASSERT_TRUE(KnownGuid::NameToGuid("fuchsia-system", guid), "fuchsia-system not found");
    ASSERT_EQ(memcmp(guid, sys, GPT_GUID_LEN), 0, "Could not lookup fuchsia-system");

    uint8_t factory[GPT_GUID_LEN] = GUID_FACTORY_CONFIG_VALUE;
    ASSERT_TRUE(KnownGuid::NameToGuid("factory", guid), "factory not found");
    ASSERT_EQ(memcmp(guid, factory, GPT_GUID_LEN), 0, "Could not lookup factory");

    uint8_t vbmeta[GPT_GUID_LEN] = GUID_VBMETA_B_VALUE;
    ASSERT_TRUE(KnownGuid::NameToGuid("vbmeta_a", guid), "vbmeta_a not found");
    ASSERT_EQ(memcmp(guid, vbmeta, GPT_GUID_LEN), 0, "Could not lookup vbmeta_a");

    END_TEST;
}

// Litmus test for guid str to name conversions
bool GuidStrToNameTest() {
    BEGIN_TEST;

    ASSERT_TRUE(
        strcmp(KnownGuid::GuidStrToName("CAB6E88E-ABF3-4102-A07A-D4BB9BE3C1D3"), "cros-firmware"),
        "No match for cros-firmware");

    ASSERT_TRUE(
        strcmp(KnownGuid::GuidStrToName("3CB8E202-3B7E-47DD-8A3C-7FF2A13CFCEC"), "cros-rootfs"),
        "No match for cros-rootfs");

    ASSERT_TRUE(
        strcmp(KnownGuid::GuidStrToName("41D0E340-57E3-954E-8C1E-17ECAC44CFF5"), "fuchsia-fvm"),
        "No match for fuchsia-fvm");

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
RUN_TEST_WRAP((SetPartitionTypeTest<4, true>))
RUN_TEST_WRAP((SetPartitionTypeTest<8, false>))
RUN_TEST_WRAP((SetPartitionGuidTest<5, true>))
RUN_TEST_WRAP((SetPartitionGuidTest<7, false>))
RUN_TEST_WRAP((SetPartitionRangeTest<14, true, FindPartitionToExpand>))
RUN_TEST_WRAP((SetPartitionRangeTest<18, false, FindPartitionToShrink>))
RUN_TEST_WRAP((PartitionVisibilityTest<2, true>))
RUN_TEST_WRAP((PartitionFlagsTest<1, true>))
RUN_TEST_WRAP((DiffsTest<9>))
RUN_TEST_MEDIUM(KnownGuidUniqueNameTest)
RUN_TEST_MEDIUM(KnownGuidUniqueGuidTest)
RUN_TEST_MEDIUM(KnownGuidUniqueStrTest)
RUN_TEST_MEDIUM(KnownGuidToStrTest)
END_TEST_CASE(libgpt_tests)
