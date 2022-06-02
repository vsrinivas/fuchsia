// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ram-nand.h"

#include <lib/fake_ddk/fake_ddk.h>
#include <stdio.h>
#include <stdlib.h>
#include <zircon/boot/image.h>
#include <zircon/process.h>

#include <atomic>
#include <memory>
#include <utility>

#include <ddk/metadata/nand.h>
#include <fbl/alloc_checker.h>
#include <zxtest/zxtest.h>

namespace {

constexpr int kPageSize = 4096;
constexpr int kOobSize = 4;
constexpr int kBlockSize = 4;
constexpr int kNumBlocks = 5;
constexpr int kNumPages = kBlockSize * kNumBlocks;

fuchsia_hardware_nand::wire::RamNandInfo BuildConfig() {
  fuchsia_hardware_nand::wire::RamNandInfo config = {};
  config.nand_info = {4096, 4, 5, 6, 0, fuchsia_hardware_nand::wire::Class::kTest, {}};
  return config;
}

TEST(RamNandTest, TrivialLifetime) {
  NandParams params(kPageSize, kBlockSize, kNumBlocks, 6, 0);  // 6 bits of ECC, no OOB.
  {
    NandDevice device(params);

    auto device_name = device.Init();
    ASSERT_TRUE(device_name.is_ok());
    EXPECT_STREQ("ram-nand-0", device_name->data());
  }
  {
    NandDevice device(params);

    auto device_name = device.Init();
    ASSERT_TRUE(device_name.is_ok());
    EXPECT_STREQ("ram-nand-1", device_name->data());
  }
}

TEST(RamNandTest, DdkLifetime) {
  NandParams params(kPageSize, kBlockSize, kNumBlocks, 6, 0);  // 6 bits of ECC, no OOB.
  NandDevice* device(new NandDevice(params, fake_ddk::kFakeParent));

  fake_ddk::Bind ddk;
  auto config = BuildConfig();
  ASSERT_OK(device->Bind(config));
  device->DdkAsyncRemove();
  EXPECT_TRUE(ddk.Ok());

  // This should delete the object, which means this test should not leak.
  device->DdkRelease();
}

TEST(RamNandTest, ExportNandConfig) {
  NandParams params(kPageSize, kBlockSize, kNumBlocks, 6, 0);
  NandDevice device(params, fake_ddk::kFakeParent);

  fuchsia_hardware_nand::wire::RamNandInfo config = BuildConfig();
  config.export_nand_config = true;
  config.partition_map.partition_count = 3;

  // Setup the first and third partitions with extra copies, and the second one with a bbt.
  memset(config.partition_map.partitions[0].unique_guid.data(), 11, ZBI_PARTITION_GUID_LEN);
  config.partition_map.partitions[0].copy_count = 12;
  config.partition_map.partitions[0].copy_byte_offset = 13;

  config.partition_map.partitions[1].first_block = 66;
  config.partition_map.partitions[1].last_block = 77;
  config.partition_map.partitions[1].hidden = true;
  config.partition_map.partitions[1].bbt = true;

  memset(config.partition_map.partitions[2].unique_guid.data(), 22, ZBI_PARTITION_GUID_LEN);
  config.partition_map.partitions[2].copy_count = 23;
  config.partition_map.partitions[2].copy_byte_offset = 24;

  nand_config_t expected = {
      .bad_block_config =
          {
              .type = 0,
              .aml_uboot =
                  {
                      .table_start_block = 66,
                      .table_end_block = 77,
                  },
          },
      .extra_partition_config_count = 2,
      .extra_partition_config = {},
  };
  memset(expected.extra_partition_config[0].type_guid, 11, ZBI_PARTITION_GUID_LEN);
  expected.extra_partition_config[0].copy_count = 12;
  expected.extra_partition_config[0].copy_byte_offset = 13;

  memset(expected.extra_partition_config[1].type_guid, 22, ZBI_PARTITION_GUID_LEN);
  expected.extra_partition_config[1].copy_count = 23;
  expected.extra_partition_config[1].copy_byte_offset = 24;

  fake_ddk::Bind ddk;
  ddk.ExpectMetadata(&expected, sizeof(expected));
  ASSERT_OK(device.Bind(config));

  int calls;
  size_t length;
  ddk.GetMetadataInfo(&calls, &length);
  EXPECT_EQ(1, calls);
  EXPECT_EQ(sizeof(expected), length);
}

TEST(RamNandTest, ExportPartitionMap) {
  NandParams params(kPageSize, kBlockSize, kNumBlocks, 6, 0);
  NandDevice device(params, fake_ddk::kFakeParent);

  fuchsia_hardware_nand::wire::RamNandInfo config = BuildConfig();
  config.export_partition_map = true;
  config.partition_map.partition_count = 3;
  memset(config.partition_map.device_guid.data(), 33, ZBI_PARTITION_GUID_LEN);

  // Setup the first and third partitions with data, and the second one hidden.
  memset(config.partition_map.partitions[0].type_guid.data(), 44, ZBI_PARTITION_GUID_LEN);
  memset(config.partition_map.partitions[0].unique_guid.data(), 45, ZBI_PARTITION_GUID_LEN);
  config.partition_map.partitions[0].first_block = 46;
  config.partition_map.partitions[0].last_block = 47;
  memset(config.partition_map.partitions[0].name.data(), 48, ZBI_PARTITION_NAME_LEN);

  config.partition_map.partitions[1].hidden = true;

  memset(config.partition_map.partitions[2].type_guid.data(), 55, ZBI_PARTITION_GUID_LEN);
  memset(config.partition_map.partitions[2].unique_guid.data(), 56, ZBI_PARTITION_GUID_LEN);
  config.partition_map.partitions[2].first_block = 57;
  config.partition_map.partitions[2].last_block = 58;
  memset(config.partition_map.partitions[2].name.data(), 59, ZBI_PARTITION_NAME_LEN);

  // Expect only two partitions on the result.
  size_t expected_size = sizeof(zbi_partition_map_t) + 2 * sizeof(zbi_partition_t);
  std::unique_ptr<char[]> buffer(new char[expected_size]);
  memset(buffer.get(), 0, expected_size);
  zbi_partition_map_t* expected = reinterpret_cast<zbi_partition_map_t*>(buffer.get());

  expected->block_count = kNumBlocks;
  expected->block_size = kPageSize * kBlockSize;
  expected->partition_count = 2;

  memset(expected->guid, 33, ZBI_PARTITION_GUID_LEN);
  memset(expected->partitions[0].type_guid, 44, ZBI_PARTITION_GUID_LEN);
  memset(expected->partitions[0].uniq_guid, 45, ZBI_PARTITION_GUID_LEN);
  expected->partitions[0].first_block = 46;
  expected->partitions[0].last_block = 47;
  memset(expected->partitions[0].name, 48, ZBI_PARTITION_NAME_LEN);

  memset(expected->partitions[1].type_guid, 55, ZBI_PARTITION_GUID_LEN);
  memset(expected->partitions[1].uniq_guid, 56, ZBI_PARTITION_GUID_LEN);
  expected->partitions[1].first_block = 57;
  expected->partitions[1].last_block = 58;
  memset(expected->partitions[1].name, 59, ZBI_PARTITION_NAME_LEN);

  fake_ddk::Bind ddk;
  ddk.ExpectMetadata(expected, expected_size);
  ASSERT_OK(device.Bind(config));

  int calls;
  size_t length;
  ddk.GetMetadataInfo(&calls, &length);
  EXPECT_EQ(1, calls);
  EXPECT_EQ(expected_size, length);
}

TEST(RamNandTest, AddMetadata) {
  NandParams params(kPageSize, kBlockSize, kNumBlocks, 6, 0);
  NandDevice device(params, fake_ddk::kFakeParent);

  fuchsia_hardware_nand::wire::RamNandInfo config = BuildConfig();
  config.export_nand_config = true;
  config.export_partition_map = true;

  fake_ddk::Bind ddk;
  ASSERT_OK(device.Bind(config));

  int calls;
  size_t length;
  ddk.GetMetadataInfo(&calls, &length);
  EXPECT_EQ(2, calls);
  EXPECT_EQ(sizeof(nand_config_t) + sizeof(zbi_partition_map_t), length);
}

std::unique_ptr<NandDevice> CreateDevice(size_t* operation_size) {
  NandParams params(kPageSize, kBlockSize, kNumBlocks, 6, kOobSize);  // 6 bits of ECC.
  fbl::AllocChecker checker;
  std::unique_ptr<NandDevice> device(new (&checker) NandDevice(params));
  if (!checker.check()) {
    return nullptr;
  }

  if (operation_size) {
    nand_info_t info;
    device->NandQuery(&info, operation_size);
  }

  if (device->Init().is_error()) {
    return nullptr;
  }
  return device;
}

TEST(RamNandTest, Unlink) {
  NandParams params(kPageSize, kBlockSize, kNumBlocks, 6, 0);
  NandDevice* device(new NandDevice(params, fake_ddk::kFakeParent));

  fake_ddk::Bind ddk;
  // We need to DdkAdd the device, as Unlink will call DdkAsyncRemove.
  auto config = BuildConfig();
  ASSERT_OK(device->Bind(config));

  auto client = fidl::BindSyncClient(ddk.FidlClient<fuchsia_hardware_nand::RamNand>());
  {
    auto result = client->Unlink();
    ASSERT_OK(result.status());
    ASSERT_OK(result.value().status);
  }
  ASSERT_OK(ddk.WaitUntilRemove());

  // The device is "dead" now.
  {
    auto result = client->Unlink();
    ASSERT_EQ(ZX_ERR_PEER_CLOSED, result.status());
  }

  // This should delete the object, which means this test should not leak.
  device->DdkRelease();
}

TEST(RamNandTest, Query) {
  NandParams params(kPageSize, kBlockSize, kNumBlocks, 6, 8);  // 6 bits of ECC, 8 OOB bytes.
  NandDevice device(params);

  nand_info_t info;
  size_t operation_size;
  device.NandQuery(&info, &operation_size);
  ASSERT_BYTES_EQ(&info, &params, sizeof(info));
  ASSERT_GT(operation_size, sizeof(nand_operation_t));
}

// Data to be pre-pended to a nand_op_t issued to the device.
struct OpHeader {
  class Operation* operation;
  class NandTest* test;
};

// Wrapper for a nand_operation_t.
class Operation {
 public:
  explicit Operation(size_t op_size, NandTest* test = 0)
      : op_size_(op_size + sizeof(OpHeader)), test_(test) {}
  ~Operation() {
    if (mapped_addr_) {
      zx_vmar_unmap(zx_vmar_root_self(), reinterpret_cast<uintptr_t>(mapped_addr_), buffer_size_);
    }
  }

  // Accessors for the memory represented by the operation's vmo.
  size_t buffer_size() const { return buffer_size_; }
  char* buffer() const { return mapped_addr_; }

  // Creates a vmo and sets the handle on the nand_operation_t.
  bool SetDataVmo();
  bool SetOobVmo();

  nand_operation_t* GetOperation();

  void OnCompletion(zx_status_t status) {
    status_ = status;
    completed_ = true;
  }

  bool completed() const { return completed_; }
  zx_status_t status() const { return status_; }

 private:
  zx_handle_t GetVmo();
  void CreateOperation();

  zx::vmo vmo_;
  char* mapped_addr_ = nullptr;
  size_t op_size_;
  NandTest* test_;
  zx_status_t status_ = ZX_ERR_ACCESS_DENIED;
  bool completed_ = false;
  static constexpr size_t buffer_size_ = (kPageSize + kOobSize) * kNumPages;
  std::unique_ptr<char[]> raw_buffer_;
  DISALLOW_COPY_ASSIGN_AND_MOVE(Operation);
};

bool Operation::SetDataVmo() {
  nand_operation_t* operation = GetOperation();
  if (!operation) {
    return false;
  }
  if (operation->command == NAND_OP_READ_BYTES || operation->command == NAND_OP_WRITE_BYTES) {
    operation->rw_bytes.data_vmo = GetVmo();
    return operation->rw_bytes.data_vmo != ZX_HANDLE_INVALID;
  }
  operation->rw.data_vmo = GetVmo();
  return operation->rw.data_vmo != ZX_HANDLE_INVALID;
}

bool Operation::SetOobVmo() {
  nand_operation_t* operation = GetOperation();
  if (!operation) {
    return false;
  }
  operation->rw.oob_vmo = GetVmo();
  return operation->rw.oob_vmo != ZX_HANDLE_INVALID;
}

nand_operation_t* Operation::GetOperation() {
  if (!raw_buffer_) {
    CreateOperation();
  }
  return reinterpret_cast<nand_operation_t*>(raw_buffer_.get() + sizeof(OpHeader));
}

zx_handle_t Operation::GetVmo() {
  if (vmo_.is_valid()) {
    return vmo_.get();
  }

  zx_status_t status = zx::vmo::create(buffer_size_, 0, &vmo_);
  if (status != ZX_OK) {
    return ZX_HANDLE_INVALID;
  }

  uintptr_t address;
  status = zx_vmar_map(zx_vmar_root_self(), ZX_VM_PERM_READ | ZX_VM_PERM_WRITE, 0, vmo_.get(), 0,
                       buffer_size_, &address);
  if (status != ZX_OK) {
    return ZX_HANDLE_INVALID;
  }
  mapped_addr_ = reinterpret_cast<char*>(address);
  return vmo_.get();
}

void Operation::CreateOperation() {
  fbl::AllocChecker checker;
  raw_buffer_.reset(new (&checker) char[op_size_]);
  if (!checker.check()) {
    return;
  }

  memset(raw_buffer_.get(), 0, op_size_);
  OpHeader* header = reinterpret_cast<OpHeader*>(raw_buffer_.get());
  header->operation = this;
  header->test = test_;
}

// Provides control primitives for tests that issue IO requests to the device.
class NandTest : public zxtest::Test {
 public:
  NandTest() {}
  ~NandTest() {}

  static void CompletionCb(void* cookie, zx_status_t status, nand_operation_t* op) {
    OpHeader* header = reinterpret_cast<OpHeader*>(reinterpret_cast<char*>(op) - sizeof(OpHeader));

    header->operation->OnCompletion(status);
    header->test->num_completed_++;
    sync_completion_signal(&header->test->event_);
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

 private:
  sync_completion_t event_;
  std::atomic<int> num_completed_ = 0;
  DISALLOW_COPY_ASSIGN_AND_MOVE(NandTest);
};

// Tests trivial attempts to queue one operation.
TEST_F(NandTest, QueueOne) {
  size_t op_size;
  std::unique_ptr<NandDevice> device = CreateDevice(&op_size);
  ASSERT_TRUE(device);

  Operation operation(op_size, this);
  nand_operation_t* op = operation.GetOperation();
  ASSERT_TRUE(op);

  op->rw.command = NAND_OP_WRITE;
  device->NandQueue(op, &NandTest::CompletionCb, nullptr);

  ASSERT_TRUE(Wait());
  ASSERT_EQ(ZX_ERR_OUT_OF_RANGE, operation.status());

  op->rw.length = 1;
  device->NandQueue(op, &NandTest::CompletionCb, nullptr);
  ASSERT_TRUE(Wait());
  ASSERT_EQ(ZX_ERR_BAD_HANDLE, operation.status());

  op->rw.offset_nand = kNumPages;
  device->NandQueue(op, &NandTest::CompletionCb, nullptr);
  ASSERT_TRUE(Wait());
  ASSERT_EQ(ZX_ERR_OUT_OF_RANGE, operation.status());

  ASSERT_TRUE(operation.SetDataVmo());

  op->rw.offset_nand = kNumPages - 1;
  device->NandQueue(op, &NandTest::CompletionCb, nullptr);
  ASSERT_TRUE(Wait());
  ASSERT_OK(operation.status());
}

// Verifies that the buffer pointed to by the operation's vmo contains the given
// pattern for the desired number of pages, skipping the pages before start.
bool CheckPattern(uint8_t what, int start, int num_pages, const Operation& operation) {
  const char* buffer = operation.buffer() + kPageSize * start;
  for (int i = 0; i < kPageSize * num_pages; i++) {
    if (static_cast<uint8_t>(buffer[i]) != what) {
      return false;
    }
  }
  return true;
}

// Prepares the operation to write num_pages starting at offset.
void SetForWrite(int offset, int num_pages, Operation* operation) {
  nand_operation_t* op = operation->GetOperation();
  op->rw.command = NAND_OP_WRITE;
  op->rw.length = num_pages;
  op->rw.offset_nand = offset;
}

// Prepares the operation to read num_pages starting at offset.
void SetForRead(int offset, int num_pages, Operation* operation) {
  nand_operation_t* op = operation->GetOperation();
  op->rw.command = NAND_OP_READ;
  op->rw.length = num_pages;
  op->rw.offset_nand = offset;
}

TEST_F(NandTest, ReadWrite) {
  size_t op_size;
  std::unique_ptr<NandDevice> device = CreateDevice(&op_size);
  ASSERT_TRUE(device);

  Operation operation(op_size, this);
  ASSERT_TRUE(operation.SetDataVmo());
  memset(operation.buffer(), 0x55, operation.buffer_size());

  nand_operation_t* op = operation.GetOperation();
  op->rw.corrected_bit_flips = 125;

  SetForWrite(4, 4, &operation);
  device->NandQueue(op, &NandTest::CompletionCb, nullptr);

  ASSERT_TRUE(Wait());
  ASSERT_OK(operation.status());
  ASSERT_EQ(125, op->rw.corrected_bit_flips);  // Doesn't modify the value.

  op->rw.command = NAND_OP_READ;
  memset(operation.buffer(), 0, operation.buffer_size());

  device->NandQueue(op, &NandTest::CompletionCb, nullptr);
  ASSERT_TRUE(Wait());
  ASSERT_OK(operation.status());
  ASSERT_EQ(0, op->rw.corrected_bit_flips);
  ASSERT_TRUE(CheckPattern(0x55, 0, 4, operation));
}

// Tests that a new device is filled with 0xff (as a new nand chip).
TEST_F(NandTest, NewChip) {
  size_t op_size;
  std::unique_ptr<NandDevice> device = CreateDevice(&op_size);
  ASSERT_TRUE(device);

  Operation operation(op_size, this);
  ASSERT_TRUE(operation.SetDataVmo());
  ASSERT_TRUE(operation.SetOobVmo());
  memset(operation.buffer(), 0x55, operation.buffer_size());

  nand_operation_t* op = operation.GetOperation();
  op->rw.corrected_bit_flips = 125;

  SetForRead(0, kNumPages, &operation);
  op->rw.offset_oob_vmo = kNumPages;
  device->NandQueue(op, &NandTest::CompletionCb, nullptr);

  ASSERT_TRUE(Wait());
  ASSERT_OK(operation.status());
  ASSERT_EQ(0, op->rw.corrected_bit_flips);

  ASSERT_TRUE(CheckPattern(0xff, 0, kNumPages, operation));

  // Verify OOB area.
  memset(operation.buffer(), 0xff, kOobSize * kNumPages);
  ASSERT_BYTES_EQ(operation.buffer() + kPageSize * kNumPages, operation.buffer(),
                  kOobSize * kNumPages);
}

// Tests serialization of multiple reads and writes.
TEST_F(NandTest, QueueMultiple) {
  size_t op_size;
  std::unique_ptr<NandDevice> device = CreateDevice(&op_size);
  ASSERT_TRUE(device);

  std::unique_ptr<Operation> operations[10];
  for (int i = 0; i < 10; i++) {
    fbl::AllocChecker checker;
    operations[i].reset(new (&checker) Operation(op_size, this));
    ASSERT_TRUE(checker.check());
    Operation& operation = *(operations[i].get());
    ASSERT_TRUE(operation.SetDataVmo());
    memset(operation.buffer(), i + 30, operation.buffer_size());
  }

  SetForWrite(0, 1, operations[0].get());  // 0 x x x x x
  SetForWrite(1, 3, operations[1].get());  // 0 1 1 1 x x
  SetForRead(0, 4, operations[2].get());
  SetForWrite(4, 2, operations[3].get());  // 0 1 1 1 3 3
  SetForRead(2, 4, operations[4].get());
  SetForWrite(2, 2, operations[5].get());  // 0 1 5 5 3 3
  SetForRead(0, 4, operations[6].get());
  SetForWrite(0, 4, operations[7].get());  // 7 7 7 7 3 3
  SetForRead(2, 4, operations[8].get());
  SetForRead(0, 2, operations[9].get());

  for (const auto& operation : operations) {
    nand_operation_t* op = operation->GetOperation();
    device->NandQueue(op, &NandTest::CompletionCb, nullptr);
  }

  ASSERT_TRUE(WaitFor(10));

  for (const auto& operation : operations) {
    ASSERT_OK(operation->status());
    ASSERT_TRUE(operation->completed());
  }

  ASSERT_TRUE(CheckPattern(30, 0, 1, *(operations[2].get())));
  ASSERT_TRUE(CheckPattern(31, 1, 3, *(operations[2].get())));

  ASSERT_TRUE(CheckPattern(31, 0, 2, *(operations[4].get())));
  ASSERT_TRUE(CheckPattern(33, 2, 2, *(operations[4].get())));

  ASSERT_TRUE(CheckPattern(30, 0, 1, *(operations[6].get())));
  ASSERT_TRUE(CheckPattern(31, 1, 1, *(operations[6].get())));
  ASSERT_TRUE(CheckPattern(35, 2, 2, *(operations[6].get())));

  ASSERT_TRUE(CheckPattern(37, 0, 2, *(operations[8].get())));
  ASSERT_TRUE(CheckPattern(33, 2, 2, *(operations[8].get())));

  ASSERT_TRUE(CheckPattern(37, 0, 2, *(operations[9].get())));
}

TEST_F(NandTest, OobLimits) {
  size_t op_size;
  std::unique_ptr<NandDevice> device = CreateDevice(&op_size);
  ASSERT_TRUE(device);

  Operation operation(op_size, this);
  nand_operation_t* op = operation.GetOperation();
  op->rw.command = NAND_OP_READ;

  device->NandQueue(op, &NandTest::CompletionCb, nullptr);
  ASSERT_TRUE(Wait());
  ASSERT_EQ(ZX_ERR_OUT_OF_RANGE, operation.status());

  op->rw.length = 1;
  device->NandQueue(op, &NandTest::CompletionCb, nullptr);
  ASSERT_TRUE(Wait());
  ASSERT_EQ(ZX_ERR_BAD_HANDLE, operation.status());

  op->rw.offset_nand = kNumPages;
  device->NandQueue(op, &NandTest::CompletionCb, nullptr);
  ASSERT_TRUE(Wait());
  ASSERT_EQ(ZX_ERR_OUT_OF_RANGE, operation.status());

  ASSERT_TRUE(operation.SetOobVmo());

  op->rw.offset_nand = kNumPages - 1;
  device->NandQueue(op, &NandTest::CompletionCb, nullptr);
  ASSERT_TRUE(Wait());
  ASSERT_OK(operation.status());

  op->rw.length = 5;
  device->NandQueue(op, &NandTest::CompletionCb, nullptr);
  ASSERT_TRUE(Wait());
  ASSERT_EQ(ZX_ERR_OUT_OF_RANGE, operation.status());
}

TEST_F(NandTest, ReadWriteOob) {
  size_t op_size;
  std::unique_ptr<NandDevice> device = CreateDevice(&op_size);
  ASSERT_TRUE(device);

  Operation operation(op_size, this);
  ASSERT_TRUE(operation.SetOobVmo());

  const char desired[kOobSize] = {'a', 'b', 'c', 'd'};
  memcpy(operation.buffer(), desired, kOobSize);

  nand_operation_t* op = operation.GetOperation();
  op->rw.corrected_bit_flips = 125;

  SetForWrite(2, 1, &operation);
  device->NandQueue(op, &NandTest::CompletionCb, nullptr);

  ASSERT_TRUE(Wait());
  ASSERT_OK(operation.status());
  ASSERT_EQ(125, op->rw.corrected_bit_flips);  // Doesn't modify the value.

  op->rw.command = NAND_OP_READ;
  op->rw.length = 2;
  op->rw.offset_nand = 1;
  memset(operation.buffer(), 0, kOobSize * 2);

  device->NandQueue(op, &NandTest::CompletionCb, nullptr);
  ASSERT_TRUE(Wait());
  ASSERT_OK(operation.status());
  ASSERT_EQ(0, op->rw.corrected_bit_flips);

  // The "second page" has the data of interest.
  ASSERT_BYTES_EQ(operation.buffer() + kOobSize, desired, kOobSize);
}

TEST_F(NandTest, ReadWriteDataAndOob) {
  size_t op_size;
  std::unique_ptr<NandDevice> device = CreateDevice(&op_size);
  ASSERT_TRUE(device);

  Operation operation(op_size, this);
  ASSERT_TRUE(operation.SetDataVmo());
  ASSERT_TRUE(operation.SetOobVmo());

  memset(operation.buffer(), 0x55, kPageSize * 2);
  memset(operation.buffer() + kPageSize * 2, 0xaa, kOobSize * 2);

  nand_operation_t* op = operation.GetOperation();
  op->rw.corrected_bit_flips = 125;

  SetForWrite(2, 2, &operation);
  op->rw.offset_oob_vmo = 2;  // OOB is right after data.
  device->NandQueue(op, &NandTest::CompletionCb, nullptr);

  ASSERT_TRUE(Wait());
  ASSERT_OK(operation.status());
  ASSERT_EQ(125, op->rw.corrected_bit_flips);  // Doesn't modify the value.

  op->rw.command = NAND_OP_READ;
  memset(operation.buffer(), 0, kPageSize * 4);

  device->NandQueue(op, &NandTest::CompletionCb, nullptr);
  ASSERT_TRUE(Wait());
  ASSERT_OK(operation.status());
  ASSERT_EQ(0, op->rw.corrected_bit_flips);

  // Verify data.
  ASSERT_TRUE(CheckPattern(0x55, 0, 2, operation));

  // Verify OOB.
  memset(operation.buffer(), 0xaa, kPageSize);
  ASSERT_BYTES_EQ(operation.buffer() + kPageSize * 2, operation.buffer(), kOobSize * 2);
}

TEST_F(NandTest, ReadWriteDataBytes) {
  size_t op_size;
  std::unique_ptr<NandDevice> device = CreateDevice(&op_size);
  ASSERT_TRUE(device);

  Operation operation(op_size, this);
  nand_operation_t* op = operation.GetOperation();
  op->rw_bytes.command = NAND_OP_WRITE_BYTES;
  op->rw_bytes.length = 2 * kPageSize;
  op->rw_bytes.offset_nand = 2 * kPageSize;
  ASSERT_TRUE(operation.SetDataVmo());

  memset(operation.buffer(), 0x55, kPageSize * 2);

  device->NandQueue(op, &NandTest::CompletionCb, nullptr);

  ASSERT_TRUE(Wait());
  ASSERT_OK(operation.status());

  op->rw_bytes.command = NAND_OP_READ_BYTES;
  memset(operation.buffer(), 0, kPageSize * 4);

  device->NandQueue(op, &NandTest::CompletionCb, nullptr);
  ASSERT_TRUE(Wait());
  ASSERT_OK(operation.status());

  // Verify data.
  ASSERT_TRUE(CheckPattern(0x55, 0, 2, operation));
}

TEST_F(NandTest, EraseLimits) {
  size_t op_size;
  std::unique_ptr<NandDevice> device = CreateDevice(&op_size);
  ASSERT_TRUE(device);

  Operation operation(op_size, this);
  ASSERT_TRUE(operation.SetDataVmo());

  nand_operation_t* op = operation.GetOperation();
  op->erase.command = NAND_OP_ERASE;

  device->NandQueue(op, &NandTest::CompletionCb, nullptr);
  ASSERT_TRUE(Wait());
  ASSERT_EQ(ZX_ERR_OUT_OF_RANGE, operation.status());

  op->erase.first_block = 5;
  op->erase.num_blocks = 1;
  device->NandQueue(op, &NandTest::CompletionCb, nullptr);
  ASSERT_TRUE(Wait());
  ASSERT_EQ(ZX_ERR_OUT_OF_RANGE, operation.status());

  op->erase.first_block = 4;
  op->erase.num_blocks = 2;
  device->NandQueue(op, &NandTest::CompletionCb, nullptr);
  ASSERT_TRUE(Wait());
  ASSERT_EQ(ZX_ERR_OUT_OF_RANGE, operation.status());
}

TEST_F(NandTest, Erase) {
  size_t op_size;
  std::unique_ptr<NandDevice> device = CreateDevice(&op_size);
  ASSERT_TRUE(device);

  Operation operation(op_size, this);
  nand_operation_t* op = operation.GetOperation();
  op->erase.command = NAND_OP_ERASE;
  op->erase.first_block = 3;
  op->erase.num_blocks = 2;

  device->NandQueue(op, &NandTest::CompletionCb, nullptr);
  ASSERT_TRUE(Wait());
  ASSERT_OK(operation.status());

  memset(op, 0, sizeof(*op));
  SetForRead(0, kNumPages, &operation);
  ASSERT_TRUE(operation.SetDataVmo());
  ASSERT_TRUE(operation.SetOobVmo());
  op->rw.offset_oob_vmo = kNumPages;
  device->NandQueue(op, &NandTest::CompletionCb, nullptr);

  ASSERT_TRUE(Wait());
  ASSERT_OK(operation.status());
  ASSERT_TRUE(CheckPattern(0xff, 0, kNumPages, operation));

  // Verify OOB area.
  memset(operation.buffer(), 0xff, kOobSize * kNumPages);
  ASSERT_BYTES_EQ(operation.buffer() + kPageSize * kNumPages, operation.buffer(),
                  kOobSize * kNumPages);
}

}  // namespace
