// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/fvm/driver/vpartition_manager.h"

#include <lib/inspect/cpp/hierarchy.h>
#include <lib/inspect/cpp/reader.h>

#include <vector>

#include <zxtest/zxtest.h>

#include "src/storage/fvm/driver/vpartition.h"
#include "src/storage/fvm/format.h"
#include "src/storage/fvm/metadata.h"

namespace fvm {

constexpr size_t kFvmSliceSize = 8 * kBlockSize;
constexpr size_t kDiskSize = 64 * kBlockSize;
constexpr uint32_t kBlocksPerSlice = 128;

// Provides a very simple ramdisk-like interface where we can track trim operations
class FakeBlockDevice : public ddk::BlockImplProtocol<FakeBlockDevice> {
 public:
  static constexpr uint32_t kBlockSize = 512;

  FakeBlockDevice() : proto_({&block_impl_protocol_ops_, this}) { data_.resize(kDiskSize); }

  block_impl_protocol_t* proto() { return &proto_; }

  // Access to the underlying data for tests to provide data or validate writes.
  const std::vector<uint8_t>& data() const { return data_; }
  std::vector<uint8_t>& data() { return data_; }

  // Block protocol:
  void BlockImplQuery(block_info_t* out_info, size_t* out_block_op_size) {
    *out_info = {};
    out_info->block_size = kBlockSize;
    out_info->block_count = kDiskSize / out_info->block_size;
    out_info->max_transfer_size = BLOCK_MAX_TRANSFER_UNBOUNDED;
    *out_block_op_size = sizeof(block_op_t);
  }

  void BlockImplQueue(block_op_t* operation, block_impl_queue_callback completion_cb,
                      void* cookie) {
    zx_status_t result = ZX_OK;
    switch (operation->command) {
      case BLOCK_OP_READ:
        // Read from device, write to VMO.
        if ((operation->rw.offset_dev + operation->rw.length) * kBlockSize <= data_.size()) {
          result = zx_vmo_write(operation->rw.vmo, &data_[operation->rw.offset_dev * kBlockSize],
                                operation->rw.offset_vmo * kBlockSize,
                                static_cast<size_t>(operation->rw.length) * kBlockSize);
        } else {
          result = ZX_ERR_OUT_OF_RANGE;
        }
        break;
      case BLOCK_OP_WRITE:
        // Write to device, read from VMO.
        if ((operation->rw.offset_dev + operation->rw.length) * kBlockSize <= data_.size()) {
          result = zx_vmo_read(operation->rw.vmo, &data_[operation->rw.offset_dev * kBlockSize],
                               operation->rw.offset_vmo * kBlockSize,
                               static_cast<size_t>(operation->rw.length) * kBlockSize);
        } else {
          result = ZX_ERR_OUT_OF_RANGE;
        }
        break;
      case BLOCK_OP_TRIM:
        num_trim_calls_++;
        last_trim_length_ += operation->trim.length;
        break;
    }
    completion_cb(cookie, result, operation);
  }

  int num_trim_calls() const { return num_trim_calls_; }
  uint32_t last_trim_length() const { return last_trim_length_; }

 private:
  block_impl_protocol_t proto_;
  int num_trim_calls_ = 0;
  uint32_t last_trim_length_ = 0;

  std::vector<uint8_t> data_;
};

TEST(VPartitionManager, TrivialLifetime) {
  FakeBlockDevice block_device;
  block_info_t info;
  size_t block_op_size;
  block_device.BlockImplQuery(&info, &block_op_size);
  VPartitionManager device(nullptr, info, block_op_size, block_device.proto());

  VPartition partition(&device, 1, block_op_size);
}

// Initializes a block device containing an FVM header with one partition with the given oldest
// revision.
template <uint64_t oldest_minor_version>
class VPartitionManagerTestAtRevision : public zxtest::Test {
 public:
  void SetUp() override {
    block_info_t info;
    block_device_.BlockImplQuery(&info, &block_op_size_);

    // Generate the FVM partition information for the initial device state. This contains no
    // partitions or allocated slices.
    Header header = Header::FromDiskSize(kMaxUsablePartitions, kDiskSize, kFvmSliceSize);
    header.oldest_minor_version = oldest_minor_version;
    auto metadata_or = Metadata::Synthesize(header, nullptr, 0u, nullptr, 0u);
    ASSERT_TRUE(metadata_or.is_ok());

    // Write the FVM data to the device.
    ASSERT_LT(metadata_or.value().Get()->size(), block_device_.data().size());
    memcpy(block_device_.data().data(), metadata_or.value().Get()->data(),
           metadata_or.value().Get()->size());

    device_ =
        std::make_unique<VPartitionManager>(nullptr, info, block_op_size_, block_device_.proto());
    device_->Load();

    ASSERT_EQ(kBlocksPerSlice, kFvmSliceSize / info.block_size);
  }

  // Returns a copy of the FVM metadata written to the block device.
  zx::result<Metadata> GetMetadata() const {
    // Need to look at the header to tell how big the metadata will be.
    Header header;
    if (block_device_.data().size() < sizeof(Header))
      return zx::error(ZX_ERR_BUFFER_TOO_SMALL);
    memcpy(&header, block_device_.data().data(), sizeof(Header));

    // Now copy the full metadata out.
    size_t metadata_size = header.GetMetadataAllocatedBytes();
    if (block_device_.data().size() < metadata_size * 2)
      return zx::error(ZX_ERR_BUFFER_TOO_SMALL);
    auto metadata_a_buffer = std::make_unique<uint8_t[]>(metadata_size);
    auto metadata_b_buffer = std::make_unique<uint8_t[]>(metadata_size);
    memcpy(metadata_a_buffer.get(), block_device_.data().data(), metadata_size);
    memcpy(metadata_b_buffer.get(),
           block_device_.data().data() + header.GetMetadataAllocatedBytes(), metadata_size);

    return Metadata::Create(
        std::make_unique<HeapMetadataBuffer>(std::move(metadata_a_buffer), metadata_size),
        std::make_unique<HeapMetadataBuffer>(std::move(metadata_b_buffer), metadata_size));
  }

  // Create a partition and returns it on success.
  zx::result<std::unique_ptr<VPartition>> AllocatePartition(const std::string& name = "name",
                                                            uint64_t slices = 1u) {
    static int next_id = 1;

    // Generates a test-unique id for the type and instance.
    fuchsia_hardware_block_partition::wire::Guid type_guid{.value = {0}};
    memcpy(type_guid.value.data(), &next_id, sizeof(int));
    next_id++;

    fuchsia_hardware_block_partition::wire::Guid instance_guid{.value = {0}};
    memcpy(instance_guid.value.data(), &next_id, sizeof(int));
    next_id++;

    return device_->AllocatePartition(slices, type_guid, instance_guid,
                                      fidl::StringView::FromExternal(name), 0);
  }

 protected:
  FakeBlockDevice block_device_;
  std::unique_ptr<VPartitionManager> device_;

  size_t block_op_size_ = 0;
};

using VPartitionManagerTest = VPartitionManagerTestAtRevision<kCurrentMinorVersion>;

// Verifies that simple TRIM commands are forwarded to the underlying device.
TEST_F(VPartitionManagerTest, QueueTrimOneSlice) {
  auto partition_or = AllocatePartition();
  ASSERT_TRUE(partition_or.is_ok());

  const uint32_t kOperationLength = 20;

  block_op_t op = {};
  op.trim.command = BLOCK_OP_TRIM;
  op.trim.length = kOperationLength;
  op.trim.offset_dev = kBlocksPerSlice / 2;

  partition_or.value()->BlockImplQueue(
      &op, [](void*, zx_status_t status, block_op_t*) {}, nullptr);
  EXPECT_EQ(1, block_device_.num_trim_calls());
  EXPECT_EQ(kOperationLength, block_device_.last_trim_length());
}

// Verifies that TRIM commands that span slices are forwarded to the underlying device.
TEST_F(VPartitionManagerTest, QueueTrimConsecutiveSlices) {
  // Ideally this should use AllocatePartition to have the VPartitionManager create the partition in
  // the correct way. This test is suspicious because pslice values aren't supposed to be zero whic
  // is used below, and haveing the VPartitionManager create the partition makes this test code
  // fail. This test should be revisited.
  auto partition = std::make_unique<VPartition>(device_.get(), 1, block_op_size_);

  const uint32_t kOperationLength = 20;
  partition->SliceSetUnsafe(0, 0);  // Suspicious value, see above.
  partition->SliceSetUnsafe(1, 1);

  block_op_t op = {};
  op.trim.command = BLOCK_OP_TRIM;
  op.trim.length = kOperationLength;
  op.trim.offset_dev = kBlocksPerSlice - kOperationLength / 2;

  partition->BlockImplQueue(
      &op, [](void*, zx_status_t status, block_op_t*) {}, nullptr);
  EXPECT_EQ(1, block_device_.num_trim_calls());
  EXPECT_EQ(kOperationLength, block_device_.last_trim_length());
}

// Verifies that TRIM commands spanning non-consecutive slices are forwarded to the underlying
// device.
TEST_F(VPartitionManagerTest, QueueTrimDisjointSlices) {
  auto partition_or = AllocatePartition();
  ASSERT_TRUE(partition_or.is_ok());

  const uint32_t kOperationLength = 20;
  partition_or.value()->SliceSetUnsafe(1, 1);
  partition_or.value()->SliceSetUnsafe(2, 5);

  block_op_t op = {};
  op.trim.command = BLOCK_OP_TRIM;
  op.trim.length = kOperationLength;
  op.trim.offset_dev = kBlocksPerSlice * 2 - kOperationLength / 2;

  partition_or.value()->BlockImplQueue(
      &op, [](void*, zx_status_t status, block_op_t*) {}, nullptr);
  EXPECT_EQ(2, block_device_.num_trim_calls());
  EXPECT_EQ(kOperationLength, block_device_.last_trim_length());
}

TEST_F(VPartitionManagerTest, InspectVmoPopulatedWithInitialState) {
  fpromise::result<inspect::Hierarchy> hierarchy =
      inspect::ReadFromVmo(device_->diagnostics().DuplicateVmo());
  ASSERT_TRUE(hierarchy.is_ok());
  const inspect::Hierarchy* mount_time = hierarchy.value().GetByPath({"fvm", "mount_time"});
  ASSERT_NE(mount_time, nullptr);

  auto* major_version =
      mount_time->node().get_property<inspect::UintPropertyValue>("major_version");
  ASSERT_NE(major_version, nullptr);
  EXPECT_EQ(major_version->value(), kCurrentMajorVersion);

  auto* oldest_minor_version =
      mount_time->node().get_property<inspect::UintPropertyValue>("oldest_minor_version");
  ASSERT_NE(oldest_minor_version, nullptr);
  EXPECT_EQ(oldest_minor_version->value(), kCurrentMinorVersion);
}

TEST_F(VPartitionManagerTest, InspectVmoTracksSliceAllocations) {
  auto partition_or = AllocatePartition("part1", 3u);
  ASSERT_TRUE(partition_or.is_ok());

  {
    fpromise::result<inspect::Hierarchy> hierarchy =
        inspect::ReadFromVmo(device_->diagnostics().DuplicateVmo());
    ASSERT_TRUE(hierarchy.is_ok());
    const inspect::Hierarchy* node = hierarchy.value().GetByPath({"fvm", "partitions", "part1"});
    ASSERT_NE(node, nullptr);
    auto* total_slices_reserved =
        node->node().get_property<inspect::UintPropertyValue>("total_slices_reserved");
    ASSERT_NE(total_slices_reserved, nullptr);
    EXPECT_EQ(total_slices_reserved->value(), 3u);
  }

  ASSERT_EQ(device_->AllocateSlices(partition_or.value().get(), 0x100000, 1), ZX_OK);

  {
    fpromise::result<inspect::Hierarchy> hierarchy =
        inspect::ReadFromVmo(device_->diagnostics().DuplicateVmo());
    ASSERT_TRUE(hierarchy.is_ok());
    const inspect::Hierarchy* node = hierarchy.value().GetByPath({"fvm", "partitions", "part1"});
    ASSERT_NE(node, nullptr);
    auto* total_slices_reserved =
        node->node().get_property<inspect::UintPropertyValue>("total_slices_reserved");
    ASSERT_NE(total_slices_reserved, nullptr);
    EXPECT_EQ(total_slices_reserved->value(), 4u);
  }
}

TEST_F(VPartitionManagerTest, InspectVmoTracksPartitionLimit) {
  constexpr uint64_t kNewSliceLimit = 4u;
  static_assert(kNewSliceLimit > 0, "Slice limit must be greater than zero for test to be valid.");
  static_assert(sizeof(guid_t) == fvm::kGuidSize, "GUID size mismatch!");

  const std::string kPartitionName = "part1";

  auto partition_or = AllocatePartition(kPartitionName, 3u);
  ASSERT_TRUE(partition_or.is_ok());

  guid_t partition_guid;
  ASSERT_EQ(partition_or->BlockPartitionGetGuid(GUIDTYPE_INSTANCE, &partition_guid), ZX_OK);
  const uint8_t* guid_as_bytes = reinterpret_cast<const uint8_t*>(&partition_guid);

  {
    // Initial limit should be set to zero.
    uint64_t slice_limit;
    ASSERT_EQ(device_->GetPartitionLimitInternal(guid_as_bytes, &slice_limit), ZX_OK);
    EXPECT_EQ(slice_limit, 0u);

    fpromise::result<inspect::Hierarchy> hierarchy =
        inspect::ReadFromVmo(device_->diagnostics().DuplicateVmo());
    ASSERT_TRUE(hierarchy.is_ok());
    const inspect::Hierarchy* node =
        hierarchy.value().GetByPath({"fvm", "partitions", kPartitionName});
    ASSERT_NE(node, nullptr);
    auto* max_bytes = node->node().get_property<inspect::UintPropertyValue>("max_bytes");
    ASSERT_NE(max_bytes, nullptr);
    EXPECT_EQ(max_bytes->value(), 0u);
  }

  device_->SetPartitionLimitInternal(guid_as_bytes, kNewSliceLimit);

  {
    // Check that the partition limit was set and is updated in Inspect.
    uint64_t slice_limit;
    ASSERT_EQ(device_->GetPartitionLimitInternal(guid_as_bytes, &slice_limit), ZX_OK);
    EXPECT_EQ(slice_limit, kNewSliceLimit);

    fpromise::result<inspect::Hierarchy> hierarchy =
        inspect::ReadFromVmo(device_->diagnostics().DuplicateVmo());
    ASSERT_TRUE(hierarchy.is_ok());
    const inspect::Hierarchy* node =
        hierarchy.value().GetByPath({"fvm", "partitions", kPartitionName});
    ASSERT_NE(node, nullptr);
    auto* max_bytes = node->node().get_property<inspect::UintPropertyValue>("max_bytes");
    ASSERT_NE(max_bytes, nullptr);
    EXPECT_EQ(max_bytes->value(), kNewSliceLimit * device_->slice_size());
  }
}

// Tests that opening a device at a newer "oldest revision" updates the device's oldest revision to
// the current revision value.
constexpr uint64_t kNextRevision = kCurrentMinorVersion + 1;
using VPartitionManagerTestAtNextRevision = VPartitionManagerTestAtRevision<kNextRevision>;
TEST_F(VPartitionManagerTestAtNextRevision, UpdateOldestRevision) {
  auto first_metadata_or = GetMetadata();
  ASSERT_TRUE(first_metadata_or.is_ok());

  auto first_metadata_type = first_metadata_or.value().active_header();

  // No operations have been performed, the FVM header will be unchanged from initialization and
  // will reference the next revision.
  EXPECT_EQ(first_metadata_or.value().GetHeader().oldest_minor_version, kNextRevision);

  // Trigger a write operation. This allocated a new partition but could be any operation that
  // forces a write to the FVM metadata.
  ASSERT_TRUE(AllocatePartition().is_ok());

  // Read the updated metadata.
  auto second_metadata_or = GetMetadata();
  ASSERT_TRUE(second_metadata_or.is_ok());

  // The active header should have swapped between the primary and secondary copy.
  auto second_metadata_type = second_metadata_or.value().active_header();
  EXPECT_NE(first_metadata_type, second_metadata_type);

  // The newly active header should have the oldest revision downgraded to the current one.
  EXPECT_EQ(second_metadata_or.value().GetHeader().oldest_minor_version, kCurrentMinorVersion);
}

// Tests that opening a device at a older "oldest revision" doesn't change the oldest revision.
constexpr uint64_t kPreviousRevision = kCurrentMinorVersion - 1;
using VPartitionManagerTestAtPreviousRevision = VPartitionManagerTestAtRevision<kPreviousRevision>;
TEST_F(VPartitionManagerTestAtPreviousRevision, DontUpdateOldestRevision) {
  auto first_metadata_or = GetMetadata();
  ASSERT_TRUE(first_metadata_or.is_ok());

  auto first_metadata_type = first_metadata_or.value().active_header();

  // No operations have been performed, the FVM header will be unchanged from initialization and
  // will reference the next revision.
  EXPECT_EQ(first_metadata_or.value().GetHeader().oldest_minor_version, kPreviousRevision);

  // Trigger a write operation. This allocated a new partition but could be any operation that
  // forces a write to the FVM metadata.
  ASSERT_TRUE(AllocatePartition().is_ok());

  // Read the updated metadata.
  auto second_metadata_or = GetMetadata();
  ASSERT_TRUE(second_metadata_or.is_ok());

  // The active header should have swapped between the primary and secondary copy.
  auto second_metadata_type = second_metadata_or.value().active_header();
  EXPECT_NE(first_metadata_type, second_metadata_type);

  // The newly active header should still have the oldest revision unchanged rather than the current
  // one.
  EXPECT_EQ(second_metadata_or.value().GetHeader().oldest_minor_version, kPreviousRevision);
}

}  // namespace fvm
