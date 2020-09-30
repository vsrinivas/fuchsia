// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "block_device.h"

#include <lib/fake_ddk/fake_ddk.h>
#include <lib/ftl/volume.h>
#include <lib/fzl/owned-vmo-mapper.h>
#include <lib/inspect/cpp/reader.h>

#include <atomic>
#include <memory>
#include <utility>

#include <ddktl/protocol/nand.h>
#include <fbl/array.h>
#include <zxtest/zxtest.h>

namespace {

constexpr uint32_t kPageSize = 1024;
constexpr uint32_t kNumPages = 20;
constexpr char kMagic = 'f';
constexpr uint8_t kGuid[ZBI_PARTITION_GUID_LEN] = {'g', 'u', 'i', 'd'};
constexpr uint32_t kWearCount = 1337;

bool CheckPattern(const void* buffer, size_t size, char pattern = kMagic) {
  const char* data = reinterpret_cast<const char*>(buffer);
  for (; size; size--) {
    if (*data++ != pattern) {
      return false;
    }
  }
  return true;
}

class FakeNand : public ddk::NandProtocol<FakeNand> {
 public:
  FakeNand() : proto_({&nand_protocol_ops_, this}) {}

  nand_protocol_t* proto() { return &proto_; }

  // Nand protocol:
  void NandQuery(fuchsia_hardware_nand_Info* out_info, size_t* out_nand_op_size) {
    *out_info = {};
    out_info->oob_size = 8;
    memcpy(out_info->partition_guid, kGuid, sizeof(kGuid));
    *out_nand_op_size = 0;
  }

  void NandQueue(nand_operation_t* operation, nand_queue_callback callback, void* cookie) {}

  zx_status_t NandGetFactoryBadBlockList(uint32_t* out_bad_blocks_list, size_t bad_blocks_count,
                                         size_t* out_bad_blocks_actual) {
    return ZX_ERR_BAD_STATE;
  }

 private:
  nand_protocol_t proto_;
};

class FakeVolume final : public ftl::Volume {
 public:
  explicit FakeVolume(ftl::BlockDevice* device) : device_(device) {}
  ~FakeVolume() final {}

  bool written() const { return written_; }
  bool flushed() const { return flushed_; }
  bool formatted() const { return formatted_; }
  bool leveled() const { return leveled_; }
  bool trimmed() const { return trimmed_; }
  uint32_t first_page() const { return first_page_; }
  int num_pages() const { return num_pages_; }

  // Volume interface.
  const char* Init(std::unique_ptr<ftl::NdmDriver> driver) final {
    device_->OnVolumeAdded(kPageSize, kNumPages);
    return nullptr;
  }
  const char* ReAttach() final { return nullptr; }
  zx_status_t Read(uint32_t first_page, int num_pages, void* buffer) final {
    first_page_ = first_page;
    num_pages_ = num_pages;
    memset(buffer, kMagic, num_pages * kPageSize);
    return ZX_OK;
  }
  zx_status_t Write(uint32_t first_page, int num_pages, const void* buffer) final {
    first_page_ = first_page;
    num_pages_ = num_pages;
    written_ = true;
    if (!CheckPattern(buffer, kPageSize * num_pages)) {
      return ZX_ERR_IO_DATA_INTEGRITY;
    }
    return ZX_OK;
  }
  zx_status_t Format() final {
    formatted_ = true;
    return ZX_OK;
  }
  zx_status_t FormatAndLevel() final {
    leveled_ = true;
    return ZX_OK;
  }
  zx_status_t Mount() final { return ZX_OK; }
  zx_status_t Unmount() final { return ZX_OK; }
  zx_status_t Flush() final {
    flushed_ = true;
    return ZX_OK;
  }
  zx_status_t Trim(uint32_t first_page, uint32_t num_pages) final {
    trimmed_ = true;
    first_page_ = first_page;
    num_pages_ = num_pages;
    return ZX_OK;
  }

  zx_status_t GarbageCollect() final { return ZX_OK; }

  zx_status_t GetStats(Stats* stats) final {
    *stats = {};
    stats->wear_count = kWearCount;
    return ZX_OK;
  }

 private:
  ftl::BlockDevice* device_;
  uint32_t first_page_ = 0;
  int num_pages_ = 0;
  bool written_ = false;
  bool flushed_ = false;
  bool formatted_ = false;
  bool leveled_ = false;
  bool trimmed_ = false;
};

TEST(BlockDeviceTest, TrivialLifetime) {
  FakeNand nand;
  ftl::BlockDevice device;
  device.SetVolumeForTest(std::make_unique<FakeVolume>(&device));
  device.SetNandParentForTest(*nand.proto());
  ASSERT_OK(device.Init());
}

TEST(BlockDeviceTest, DdkLifetime) {
  ftl::BlockDevice* device(new ftl::BlockDevice(fake_ddk::kFakeParent));
  device->SetVolumeForTest(std::make_unique<FakeVolume>(device));

  FakeNand nand;
  fake_ddk::Bind ddk;
  fbl::Array<fake_ddk::ProtocolEntry> protocols(new fake_ddk::ProtocolEntry[1], 1);
  protocols[0] = {ZX_PROTOCOL_NAND, {nand.proto()->ops, nand.proto()->ctx}};
  ddk.SetProtocols(std::move(protocols));

  ASSERT_OK(device->Bind());
  device->DdkAsyncRemove();
  EXPECT_TRUE(ddk.Ok());

  // This should delete the object, which means this test should not leak.
  device->DdkRelease();
}

TEST(BlockDeviceTest, GetSize) {
  FakeNand nand;
  ftl::BlockDevice device;
  device.SetVolumeForTest(std::make_unique<FakeVolume>(&device));
  device.SetNandParentForTest(*nand.proto());
  ASSERT_OK(device.Init());
  EXPECT_EQ(kPageSize * kNumPages, device.DdkGetSize());
}

TEST(BlockDeviceTest, GetName) {
  FakeNand nand;
  ftl::BlockDevice device;
  device.SetVolumeForTest(std::make_unique<FakeVolume>(&device));
  device.SetNandParentForTest(*nand.proto());
  ASSERT_OK(device.Init());

  char name[20];
  ASSERT_OK(device.BlockPartitionGetName(name, sizeof(name)));

  EXPECT_GT(strlen(name), 0);
}

TEST(BlockDeviceTest, GetType) {
  FakeNand nand;
  ftl::BlockDevice device;
  device.SetVolumeForTest(std::make_unique<FakeVolume>(&device));
  device.SetNandParentForTest(*nand.proto());
  ASSERT_OK(device.Init());

  guid_t guid;
  ASSERT_OK(device.BlockPartitionGetGuid(GUIDTYPE_TYPE, &guid));

  EXPECT_EQ(0, memcmp(&guid, kGuid, sizeof(guid)));
}

TEST(BlockDeviceTest, Query) {
  FakeNand nand;
  ftl::BlockDevice device;
  device.SetVolumeForTest(std::make_unique<FakeVolume>(&device));
  device.SetNandParentForTest(*nand.proto());
  ASSERT_OK(device.Init());

  block_info_t info;
  size_t operation_size;
  device.BlockImplQuery(&info, &operation_size);

  constexpr block_info_t kInfo = {kNumPages, kPageSize, BLOCK_MAX_TRANSFER_UNBOUNDED,
                                  BLOCK_FLAG_TRIM_SUPPORT, 0};

  ASSERT_BYTES_EQ(&info, &kInfo, sizeof(info));
  ASSERT_GT(operation_size, sizeof(block_op_t));
}

class BlockDeviceTest;

// Wrapper for a block_op_t.
class Operation {
 public:
  explicit Operation(size_t op_size, BlockDeviceTest* test) : op_size_(op_size), test_(test) {}
  ~Operation() {}

  // Accessors for the memory represented by the operation's vmo.
  size_t buffer_size() const { return buffer_size_; }
  void* buffer() const { return mapper_.start(); }

  // Creates a vmo and sets the handle on the block_op_t.
  bool SetVmo();

  block_op_t* GetOperation();

  void OnCompletion(zx_status_t status) {
    status_ = status;
    completed_ = true;
  }

  bool completed() const { return completed_; }
  zx_status_t status() const { return status_; }
  BlockDeviceTest* test() const { return test_; }

  DISALLOW_COPY_ASSIGN_AND_MOVE(Operation);

 private:
  zx_handle_t GetVmo();

  fzl::OwnedVmoMapper mapper_;
  size_t op_size_;
  BlockDeviceTest* test_;
  zx_status_t status_ = ZX_ERR_ACCESS_DENIED;
  bool completed_ = false;
  static constexpr size_t buffer_size_ = kPageSize * kNumPages;
  std::unique_ptr<char[]> raw_buffer_;
};

bool Operation::SetVmo() {
  block_op_t* operation = GetOperation();
  if (!operation) {
    return false;
  }
  operation->rw.vmo = GetVmo();
  return operation->rw.vmo != ZX_HANDLE_INVALID;
}

block_op_t* Operation::GetOperation() {
  if (!raw_buffer_) {
    raw_buffer_.reset(new char[op_size_]);
    memset(raw_buffer_.get(), 0, op_size_);
  }
  return reinterpret_cast<block_op_t*>(raw_buffer_.get());
}

zx_handle_t Operation::GetVmo() {
  if (mapper_.start()) {
    return mapper_.vmo().get();
  }

  if (mapper_.CreateAndMap(buffer_size_, "") != ZX_OK) {
    return ZX_HANDLE_INVALID;
  }

  return mapper_.vmo().get();
}

// Provides control primitives for tests that issue IO requests to the device.
class BlockDeviceTest : public zxtest::Test {
 public:
  BlockDeviceTest();
  ~BlockDeviceTest() {}

  ftl::BlockDevice* GetDevice() { return device_.get(); }
  size_t op_size() const { return op_size_; }
  FakeVolume* GetVolume() { return volume_; }

  static void CompletionCb(void* cookie, zx_status_t status, block_op_t* op) {
    Operation* operation = reinterpret_cast<Operation*>(cookie);

    operation->OnCompletion(status);
    operation->test()->num_completed_++;
    sync_completion_signal(&operation->test()->event_);
  }

  bool Wait() {
    zx_status_t status = sync_completion_wait(&event_, ZX_SEC(5));
    sync_completion_reset(&event_);
    return status == ZX_OK;
  }

  bool WaitFor(int desired) {
    while (num_completed_ < desired) {
      if (!Wait()) {
        return false;
      }
    }
    return true;
  }

  DISALLOW_COPY_ASSIGN_AND_MOVE(BlockDeviceTest);

 private:
  sync_completion_t event_;
  std::atomic<int> num_completed_ = 0;
  std::unique_ptr<ftl::BlockDevice> device_;
  size_t op_size_;
  FakeNand nand_;
  FakeVolume* volume_ = nullptr;  // Object owned by device_.
};

BlockDeviceTest::BlockDeviceTest() : device_(new ftl::BlockDevice()) {
  volume_ = new FakeVolume(device_.get());
  device_->SetVolumeForTest(std::unique_ptr<FakeVolume>(volume_));
  device_->SetNandParentForTest(*nand_.proto());

  block_info_t info;
  device_->BlockImplQuery(&info, &op_size_);

  if (device_->Init() != ZX_OK) {
    device_.reset();
  }
}

// Tests trivial attempts to queue one operation.
TEST_F(BlockDeviceTest, QueueOne) {
  ftl::BlockDevice* device = GetDevice();
  ASSERT_TRUE(device);

  Operation operation(op_size(), this);

  block_op_t* op = operation.GetOperation();
  ASSERT_TRUE(op);

  op->rw.command = BLOCK_OP_READ;
  device->BlockImplQueue(op, &BlockDeviceTest::CompletionCb, &operation);

  ASSERT_TRUE(Wait());
  ASSERT_EQ(ZX_ERR_OUT_OF_RANGE, operation.status());

  op->rw.length = 1;
  device->BlockImplQueue(op, &BlockDeviceTest::CompletionCb, &operation);
  ASSERT_TRUE(Wait());
  ASSERT_EQ(ZX_ERR_INVALID_ARGS, operation.status());

  op->rw.offset_dev = kNumPages;
  device->BlockImplQueue(op, &BlockDeviceTest::CompletionCb, &operation);
  ASSERT_TRUE(Wait());
  ASSERT_EQ(ZX_ERR_OUT_OF_RANGE, operation.status());

  ASSERT_TRUE(operation.SetVmo());

  op->rw.offset_dev = kNumPages - 1;
  device->BlockImplQueue(op, &BlockDeviceTest::CompletionCb, &operation);
  ASSERT_TRUE(Wait());
  ASSERT_OK(operation.status());
}

TEST_F(BlockDeviceTest, ReadWrite) {
  ftl::BlockDevice* device = GetDevice();
  ASSERT_TRUE(device);

  Operation operation(op_size(), this);
  ASSERT_TRUE(operation.SetVmo());

  block_op_t* op = operation.GetOperation();
  ASSERT_TRUE(op);

  op->rw.command = BLOCK_OP_READ;
  op->rw.length = 2;
  op->rw.offset_dev = 3;
  ASSERT_TRUE(operation.SetVmo());
  device->BlockImplQueue(op, &BlockDeviceTest::CompletionCb, &operation);

  ASSERT_TRUE(Wait());
  ASSERT_OK(operation.status());

  FakeVolume* volume = GetVolume();
  EXPECT_FALSE(volume->written());
  EXPECT_EQ(2, volume->num_pages());
  EXPECT_EQ(3, volume->first_page());
  EXPECT_TRUE(CheckPattern(operation.buffer(), kPageSize * 2));

  op->rw.command = BLOCK_OP_WRITE;
  op->rw.length = 4;
  op->rw.offset_dev = 5;
  memset(operation.buffer(), kMagic, kPageSize * 5);
  device->BlockImplQueue(op, &BlockDeviceTest::CompletionCb, &operation);

  ASSERT_TRUE(Wait());
  ASSERT_OK(operation.status());

  EXPECT_TRUE(volume->written());
  EXPECT_EQ(4, volume->num_pages());
  EXPECT_EQ(5, volume->first_page());
}

TEST_F(BlockDeviceTest, Trim) {
  ftl::BlockDevice* device = GetDevice();
  ASSERT_TRUE(device);

  Operation operation(op_size(), this);
  block_op_t* op = operation.GetOperation();
  ASSERT_TRUE(op);

  op->trim.command = BLOCK_OP_TRIM;
  device->BlockImplQueue(op, &BlockDeviceTest::CompletionCb, &operation);

  ASSERT_TRUE(Wait());
  ASSERT_EQ(ZX_ERR_OUT_OF_RANGE, operation.status());

  op->trim.length = 2;
  op->trim.offset_dev = kNumPages - 1;
  device->BlockImplQueue(op, &BlockDeviceTest::CompletionCb, &operation);

  ASSERT_TRUE(Wait());
  ASSERT_EQ(ZX_ERR_OUT_OF_RANGE, operation.status());

  op->trim.offset_dev = 3;
  device->BlockImplQueue(op, &BlockDeviceTest::CompletionCb, &operation);

  ASSERT_TRUE(Wait());
  ASSERT_OK(operation.status());

  EXPECT_TRUE(GetVolume()->trimmed());
  EXPECT_EQ(2, GetVolume()->num_pages());
  EXPECT_EQ(3, GetVolume()->first_page());
}

TEST_F(BlockDeviceTest, Flush) {
  ftl::BlockDevice* device = GetDevice();
  ASSERT_TRUE(device);

  Operation operation(op_size(), this);
  block_op_t* op = operation.GetOperation();
  ASSERT_TRUE(op);

  op->rw.command = BLOCK_OP_FLUSH;
  device->BlockImplQueue(op, &BlockDeviceTest::CompletionCb, &operation);

  ASSERT_TRUE(Wait());
  ASSERT_OK(operation.status());

  EXPECT_TRUE(GetVolume()->flushed());
}

// Tests serialization of multiple operations.
TEST_F(BlockDeviceTest, QueueMultiple) {
  ftl::BlockDevice* device = GetDevice();
  ASSERT_TRUE(device);

  std::unique_ptr<Operation> operations[10];
  for (int i = 0; i < 10; i++) {
    operations[i].reset(new Operation(op_size(), this));
    Operation& operation = *(operations[i].get());
    block_op_t* op = operation.GetOperation();
    ASSERT_TRUE(op);

    op->rw.command = BLOCK_OP_READ;
    op->rw.length = 1;
    op->rw.offset_dev = i;
    ASSERT_TRUE(operation.SetVmo());
    device->BlockImplQueue(op, &BlockDeviceTest::CompletionCb, &operation);
  }

  ASSERT_TRUE(WaitFor(10));

  for (const auto& operation : operations) {
    ASSERT_OK(operation->status());
    ASSERT_TRUE(operation->completed());
  }
}

TEST_F(BlockDeviceTest, Format) {
  ftl::BlockDevice* device = GetDevice();
  ASSERT_TRUE(device);

  EXPECT_OK(device->Format());
  EXPECT_TRUE(GetVolume()->formatted());
  EXPECT_FALSE(GetVolume()->leveled());
}

TEST_F(BlockDeviceTest, GetInspectVmo) {
  ftl::BlockDevice* device = GetDevice();
  ASSERT_TRUE(device);

  zx::vmo vmo = device->GetInspectVmo();
  auto hierarchy = inspect::ReadFromVmo(vmo).take_value();
  auto* wear_count = hierarchy.node().get_property<inspect::UintPropertyValue>("wear_count");
  EXPECT_EQ(kWearCount, wear_count->value());
}

TEST_F(BlockDeviceTest, Suspend) {
  ftl::BlockDevice* device = GetDevice();
  ASSERT_TRUE(device);
  device->Suspend();
  EXPECT_TRUE(GetVolume()->flushed());
}

}  // namespace
