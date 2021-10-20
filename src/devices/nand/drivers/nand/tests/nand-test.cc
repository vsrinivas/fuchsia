// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/nand/drivers/nand/nand.h"

#include <lib/fake_ddk/fake_ddk.h>
#include <lib/fzl/owned-vmo-mapper.h>
#include <lib/sync/completion.h>
#include <lib/zircon-internal/thread_annotations.h>
#include <zircon/errors.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <utility>

#include <fbl/auto_lock.h>
#include <fbl/mutex.h>
#include <zxtest/zxtest.h>

#include "fuchsia/hardware/nand/c/banjo.h"
#include "lib/inspect/cpp/hierarchy.h"
#include "lib/inspect/cpp/inspector.h"
#include "lib/inspect/cpp/reader.h"
#include "lib/inspect/cpp/vmo/types.h"

namespace {

constexpr uint32_t kPageSize = 1024;
constexpr uint32_t kOobSize = 8;
constexpr uint32_t kNumPages = 20;
constexpr uint32_t kNumBlocks = 10;
constexpr uint32_t kEccBits = 10;
constexpr uint32_t kNumOobSize = 8;

constexpr uint8_t kMagic = 'd';
constexpr uint8_t kOobMagic = 'o';

nand_info_t kInfo = {kPageSize, kNumPages, kNumBlocks, kEccBits, kNumOobSize, 0, {}};

enum class OperationType {
  kRead,
  kWrite,
  kErase,
};

struct LastOperation {
  OperationType type;
  uint32_t nandpage;
};

// Fake for the raw nand protocol.
class FakeRawNand : public ddk::RawNandProtocol<FakeRawNand> {
 public:
  FakeRawNand() : proto_({&raw_nand_protocol_ops_, this}) {}

  const raw_nand_protocol_t* proto() const { return &proto_; }

  void set_result(zx_status_t result) { result_ = result; }
  void set_ecc_bits(uint32_t ecc_bits) { ecc_bits_ = ecc_bits; }
  void set_read_callback(std::function<void(FakeRawNand*)> read_callback) {
    read_callback_ = std::move(read_callback);
  }

  // Raw nand protocol:
  zx_status_t RawNandGetNandInfo(nand_info_t* out_info) {
    *out_info = info_;
    return result_;
  }

  zx_status_t RawNandReadPageHwecc(uint32_t nandpage, uint8_t* out_data_buffer, size_t data_size,
                                   size_t* out_data_actual, uint8_t* out_oob_buffer,
                                   size_t oob_size, size_t* out_oob_actual,
                                   uint32_t* out_ecc_correct) {
    if (read_callback_)
      read_callback_(this);
    if (nandpage > info_.pages_per_block * info_.num_blocks) {
      result_ = ZX_ERR_IO;
    }
    static_cast<uint8_t*>(out_data_buffer)[0] = kMagic;
    static_cast<uint8_t*>(out_oob_buffer)[0] = kOobMagic;
    *out_ecc_correct = ecc_bits_;

    fbl::AutoLock al(&lock_);
    last_op_.type = OperationType::kRead;
    last_op_.nandpage = nandpage;

    return result_;
  }

  zx_status_t RawNandWritePageHwecc(const uint8_t* data_buffer, size_t data_size,
                                    const uint8_t* oob_buffer, size_t oob_size, uint32_t nandpage) {
    if (nandpage > info_.pages_per_block * info_.num_blocks) {
      result_ = ZX_ERR_IO;
    }

    uint8_t byte = static_cast<const uint8_t*>(data_buffer)[0];
    if (byte != kMagic) {
      result_ = ZX_ERR_IO;
    }

    byte = static_cast<const uint8_t*>(oob_buffer)[0];
    if (byte != kOobMagic) {
      result_ = ZX_ERR_IO;
    }

    fbl::AutoLock al(&lock_);
    last_op_.type = OperationType::kWrite;
    last_op_.nandpage = nandpage;

    return result_;
  }

  zx_status_t RawNandEraseBlock(uint32_t nandpage) {
    fbl::AutoLock al(&lock_);
    last_op_.type = OperationType::kErase;
    last_op_.nandpage = nandpage;
    return result_;
  }

  LastOperation last_op() {
    fbl::AutoLock al(&lock_);
    return last_op_;
  }

 private:
  raw_nand_protocol_t proto_;
  nand_info_t info_ = kInfo;
  zx_status_t result_ = ZX_OK;
  uint32_t ecc_bits_ = 0;
  // Calls a specified callback passing "this" at the beginning of the RawNandReadPageHwecc.
  std::function<void(FakeRawNand*)> read_callback_ = {};

  fbl::Mutex lock_;
  LastOperation last_op_ TA_GUARDED(lock_) = {};
};

class NandTest : public zxtest::Test {
 public:
  NandTest() {
    ddk_.SetProtocol(ZX_PROTOCOL_RAW_NAND, raw_nand_.proto());
    ddk_.SetSize(kPageSize * kNumPages * kNumBlocks);
  }

  fake_ddk::Bind& ddk() { return ddk_; }
  FakeRawNand& raw_nand() { return raw_nand_; }

 private:
  fake_ddk::Bind ddk_;
  FakeRawNand raw_nand_;
};

TEST_F(NandTest, TrivialLifetime) {
  nand::NandDevice device(fake_ddk::kFakeParent);
  ASSERT_OK(device.Init());
}

TEST_F(NandTest, DdkLifetime) {
  nand::NandDevice* device(new nand::NandDevice(fake_ddk::kFakeParent));

  ASSERT_OK(device->Init());
  ASSERT_OK(device->Bind());
  device->DdkAsyncRemove();
  EXPECT_TRUE(ddk().Ok());

  // This should delete the object, which means this test should not leak.
  device->DdkRelease();
}

TEST_F(NandTest, GetSize) {
  nand::NandDevice device(fake_ddk::kFakeParent);
  ASSERT_OK(device.Init());
  EXPECT_EQ(kPageSize * kNumPages * kNumBlocks, device.DdkGetSize());
}

TEST_F(NandTest, Query) {
  nand::NandDevice device(fake_ddk::kFakeParent);
  ASSERT_OK(device.Init());

  nand_info_t info;
  size_t operation_size;
  device.NandQuery(&info, &operation_size);

  ASSERT_BYTES_EQ(&info, &kInfo, sizeof(info));
  ASSERT_GT(operation_size, sizeof(nand_operation_t));
}

class NandDeviceTest;

// Wrapper for a nand_operation_t.
class Operation {
 public:
  explicit Operation(size_t op_size, NandDeviceTest* test) : op_size_(op_size), test_(test) {}
  ~Operation() {}

  // Accessors for the memory represented by the operation's vmo.
  size_t buffer_size() const { return buffer_size_; }
  void* buffer() const { return data_mapper_.start(); }

  size_t oob_buffer_size() const { return buffer_size_; }
  void* oob_buffer() const { return oob_mapper_.start(); }

  // Creates a vmo and sets the handle on the nand_operation_t.
  bool SetVmo();

  nand_operation_t* GetOperation();

  void OnCompletion(zx_status_t status) {
    status_ = status;
    completed_ = true;
  }

  bool completed() const { return completed_; }
  zx_status_t status() const { return status_; }
  NandDeviceTest* test() const { return test_; }

  DISALLOW_COPY_ASSIGN_AND_MOVE(Operation);

 private:
  zx_handle_t GetDataVmo();
  zx_handle_t GetOobVmo();

  fzl::OwnedVmoMapper data_mapper_;
  fzl::OwnedVmoMapper oob_mapper_;
  size_t op_size_;
  NandDeviceTest* test_;
  zx_status_t status_ = ZX_ERR_ACCESS_DENIED;
  bool completed_ = false;
  static constexpr size_t buffer_size_ = kNumBlocks * kPageSize * kNumPages;
  static constexpr size_t oob_buffer_size_ = kNumBlocks * kPageSize * kNumPages;
  std::unique_ptr<char[]> raw_buffer_;
};

bool Operation::SetVmo() {
  nand_operation_t* operation = GetOperation();
  if (!operation) {
    return false;
  }
  operation->rw.data_vmo = GetDataVmo();
  operation->rw.oob_vmo = GetOobVmo();
  return operation->rw.data_vmo != ZX_HANDLE_INVALID && operation->rw.oob_vmo != ZX_HANDLE_INVALID;
}

nand_operation_t* Operation::GetOperation() {
  if (!raw_buffer_) {
    raw_buffer_.reset(new char[op_size_]);
    memset(raw_buffer_.get(), 0, op_size_);
  }
  return reinterpret_cast<nand_operation_t*>(raw_buffer_.get());
}

zx_handle_t Operation::GetDataVmo() {
  if (data_mapper_.start()) {
    return data_mapper_.vmo().get();
  }

  if (data_mapper_.CreateAndMap(buffer_size_, "") != ZX_OK) {
    return ZX_HANDLE_INVALID;
  }

  return data_mapper_.vmo().get();
}

zx_handle_t Operation::GetOobVmo() {
  if (oob_mapper_.start()) {
    return oob_mapper_.vmo().get();
  }

  if (oob_mapper_.CreateAndMap(oob_buffer_size_, "") != ZX_OK) {
    return ZX_HANDLE_INVALID;
  }

  return oob_mapper_.vmo().get();
}

// Provides control primitives for tests that issue IO requests to the device.
class NandDeviceTest : public NandTest {
 public:
  NandDeviceTest();
  ~NandDeviceTest() {}

  nand::NandDevice* device() { return device_.get(); }

  size_t op_size() const { return op_size_; }

  static void CompletionCb(void* cookie, zx_status_t status, nand_operation_t* op) {
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

 private:
  sync_completion_t event_;
  std::atomic<int> num_completed_ = 0;
  std::unique_ptr<nand::NandDevice> device_;
  size_t op_size_;
};

NandDeviceTest::NandDeviceTest() {
  device_ = std::make_unique<nand::NandDevice>(fake_ddk::kFakeParent);
  ASSERT_NOT_NULL(device_.get());

  nand_info_t info;
  device_->NandQuery(&info, &op_size_);

  ASSERT_EQ(device_->Init(), ZX_OK);
}

// Tests trivial attempts to queue one operation.
TEST_F(NandDeviceTest, QueueOne) {
  Operation operation(op_size(), this);

  nand_operation_t* op = operation.GetOperation();
  ASSERT_NOT_NULL(op);

  op->rw.command = NAND_OP_READ;
  device()->NandQueue(op, &NandDeviceTest::CompletionCb, &operation);

  ASSERT_TRUE(Wait());
  ASSERT_EQ(ZX_ERR_OUT_OF_RANGE, operation.status());

  op->rw.length = 1;
  device()->NandQueue(op, &NandDeviceTest::CompletionCb, &operation);
  ASSERT_TRUE(Wait());
  ASSERT_EQ(ZX_ERR_BAD_HANDLE, operation.status());

  op->rw.offset_nand = kNumPages * kNumBlocks;
  device()->NandQueue(op, &NandDeviceTest::CompletionCb, &operation);
  ASSERT_TRUE(Wait());
  ASSERT_STATUS(ZX_ERR_OUT_OF_RANGE, operation.status());

  ASSERT_TRUE(operation.SetVmo());

  op->rw.offset_nand = (kNumPages * kNumBlocks) - 1;
  device()->NandQueue(op, &NandDeviceTest::CompletionCb, &operation);
  ASSERT_TRUE(Wait());
  ASSERT_OK(operation.status());
}

TEST_F(NandDeviceTest, ReadWrite) {
  Operation operation(op_size(), this);
  ASSERT_TRUE(operation.SetVmo());

  nand_operation_t* op = operation.GetOperation();
  ASSERT_NOT_NULL(op);

  op->rw.command = NAND_OP_READ;
  op->rw.length = 2;
  op->rw.offset_nand = 3;
  ASSERT_TRUE(operation.SetVmo());
  device()->NandQueue(op, &NandDeviceTest::CompletionCb, &operation);

  ASSERT_TRUE(Wait());
  ASSERT_OK(operation.status());

  EXPECT_EQ(raw_nand().last_op().type, OperationType::kRead);
  EXPECT_EQ(raw_nand().last_op().nandpage, 4);

  op->rw.command = NAND_OP_WRITE;
  op->rw.length = 4;
  op->rw.offset_nand = 5;
  memset(operation.buffer(), kMagic, kPageSize * 5);
  memset(operation.oob_buffer(), kOobMagic, kOobSize * 5);
  device()->NandQueue(op, &NandDeviceTest::CompletionCb, &operation);

  ASSERT_TRUE(Wait());
  ASSERT_OK(operation.status());

  EXPECT_EQ(raw_nand().last_op().type, OperationType::kWrite);
  EXPECT_EQ(raw_nand().last_op().nandpage, 8);
}

TEST_F(NandDeviceTest, ReadWriteVmoOffsets) {
  Operation operation(op_size(), this);
  ASSERT_TRUE(operation.SetVmo());

  nand_operation_t* op = operation.GetOperation();
  ASSERT_NOT_NULL(op);

  for (uint32_t offset = 0; offset < kNumPages * kNumBlocks; offset++) {
    for (uint32_t length = 1; offset + length < kNumPages * kNumBlocks; length++) {
      op->rw.command = NAND_OP_READ;
      op->rw.length = length;
      op->rw.offset_nand = offset;
      op->rw.offset_data_vmo = offset;
      op->rw.offset_oob_vmo = offset;
      device()->NandQueue(op, &NandDeviceTest::CompletionCb, &operation);

      ASSERT_TRUE(Wait());
      ASSERT_OK(operation.status(), "offset: %d length: %d", offset, length);

      EXPECT_EQ(raw_nand().last_op().type, OperationType::kRead);
      EXPECT_EQ(raw_nand().last_op().nandpage, offset + length - 1);

      op->rw.command = NAND_OP_WRITE;
      op->rw.length = length;
      op->rw.offset_nand = offset;
      op->rw.offset_data_vmo = offset;
      op->rw.offset_oob_vmo = offset;
      memset(static_cast<uint8_t*>(operation.buffer()) + (offset * kPageSize), kMagic,
             kPageSize * length);
      memset(static_cast<uint8_t*>(operation.oob_buffer()) + (offset * kPageSize), kOobMagic,
             kOobSize * length);
      device()->NandQueue(op, &NandDeviceTest::CompletionCb, &operation);

      ASSERT_TRUE(Wait());
      ASSERT_OK(operation.status());

      EXPECT_EQ(raw_nand().last_op().type, OperationType::kWrite);
      EXPECT_EQ(raw_nand().last_op().nandpage, length + offset - 1);
    }
  }
}

TEST_F(NandDeviceTest, Erase) {
  Operation operation(op_size(), this);
  nand_operation_t* op = operation.GetOperation();
  ASSERT_NOT_NULL(op);

  op->erase.command = NAND_OP_ERASE;
  op->erase.num_blocks = 1;
  op->erase.first_block = 5;
  device()->NandQueue(op, &NandDeviceTest::CompletionCb, &operation);

  ASSERT_TRUE(Wait());
  ASSERT_OK(operation.status());

  EXPECT_EQ(raw_nand().last_op().type, OperationType::kErase);
  EXPECT_EQ(raw_nand().last_op().nandpage, 5 * kNumPages);
}

// Tests serialization of multiple operations.
TEST_F(NandDeviceTest, QueryMultiple) {
  std::unique_ptr<Operation> operations[10];
  for (int i = 0; i < 10; i++) {
    operations[i].reset(new Operation(op_size(), this));
    Operation& operation = *(operations[i].get());
    nand_operation_t* op = operation.GetOperation();
    ASSERT_NOT_NULL(op);

    op->rw.command = NAND_OP_READ;
    op->rw.length = 1;
    op->rw.offset_nand = i;
    ASSERT_TRUE(operation.SetVmo());
    device()->NandQueue(op, &NandDeviceTest::CompletionCb, &operation);
  }

  ASSERT_TRUE(WaitFor(10));

  for (const auto& operation : operations) {
    ASSERT_OK(operation->status());
    ASSERT_TRUE(operation->completed());
  }
}

struct ReadMetrics {
  uint64_t ecc_bit_flips[32];
  uint64_t ecc_bit_flips_overflow;
  uint64_t attempts[9];
  uint64_t attempts_overflow;
  uint64_t internal_failure;
  uint64_t failure;
};

void ExpectUintPropertyMatches(const inspect::Hierarchy* hierarchy,
                               const std::string& property_name, uint64_t value) {
  auto* property = hierarchy->node().get_property<inspect::UintPropertyValue>(property_name);
  EXPECT_NOT_NULL(property, "Missing property: %s", property_name.c_str());
  EXPECT_EQ(property->value(), value, "Values do not match for %s", property_name.c_str());
}

void ExpectUintHistogramMatches(const inspect::Hierarchy* hierarchy,
                                const std::string& property_name, uint64_t histogram_size,
                                const uint64_t* values, uint64_t overflow) {
  auto* property = hierarchy->node().get_property<inspect::UintArrayValue>(property_name);
  ASSERT_NOT_NULL(property, "Missing property: %s", property_name.c_str());
  auto histogram = property->GetBuckets();
  // Verify the overflow count.
  EXPECT_EQ(overflow, histogram.back().count, "Failed overflow check for %s",
            property_name.c_str());
  // Remove the underflow and overflow buckets to simplify indexing.
  histogram.pop_back();
  histogram.erase(histogram.begin());
  ASSERT_EQ(histogram.size(), histogram_size, "Histogram %s was not expecte size.",
            property_name.c_str());
  for (uint64_t i = 0; i < histogram_size; i++) {
    EXPECT_EQ(values[i], histogram[i].count, "Failed at histogram index %lu for %s", i,
              property_name.c_str());
  }
}

void ExpectMetricsMatch(const ReadMetrics& expected, const zx::vmo& inspect_vmo) {
  auto base_hierarchy = inspect::ReadFromVmo(inspect_vmo).take_value();
  auto* hierarchy = base_hierarchy.GetByPath({"nand"});
  ASSERT_NOT_NULL(hierarchy);
  ExpectUintPropertyMatches(hierarchy, "read_internal_failure", expected.internal_failure);
  ExpectUintPropertyMatches(hierarchy, "read_failure", expected.failure);
  ExpectUintHistogramMatches(hierarchy, "read_ecc_bit_flips", 32, expected.ecc_bit_flips,
                             expected.ecc_bit_flips_overflow);
  ExpectUintHistogramMatches(hierarchy, "read_attempts", 9, expected.attempts,
                             expected.attempts_overflow);
}

TEST_F(NandDeviceTest, ReadMetrics) {
  // Read different pages every time to avoid caching effects.
  Operation operation(op_size(), this);
  ASSERT_TRUE(operation.SetVmo());

  nand_operation_t* op = operation.GetOperation();
  ASSERT_NOT_NULL(op);

  // Check that everything is zeroes.
  ReadMetrics expected;
  memset(&expected, 0, sizeof(expected));
  zx::vmo inspect_vmo = device()->GetDuplicateInspectVmoForTest();
  ASSERT_NO_FAILURES(ExpectMetricsMatch(expected, inspect_vmo));

  // Normal read.
  op->rw.command = NAND_OP_READ;
  op->rw.length = 1;
  op->rw.offset_nand = 3;
  ASSERT_TRUE(operation.SetVmo());
  device()->NandQueue(op, &NandDeviceTest::CompletionCb, &operation);
  ASSERT_TRUE(Wait());
  ASSERT_OK(operation.status());

  expected.attempts[1] += 1;
  expected.ecc_bit_flips[0] += 1;
  ASSERT_NO_FAILURES(ExpectMetricsMatch(expected, inspect_vmo));

  FakeRawNand& nand = raw_nand();
  nand_info_t info;
  ASSERT_OK(nand.RawNandGetNandInfo(&info));
  size_t ecc_limit = info.ecc_bits;
  int retries = 3;
  nand.set_read_callback([&retries](FakeRawNand* n) {
    if (--retries == 0) {
      n->set_result(ZX_OK);
    }
  });

  // Fails ECC a few times before succeeding with bit flips.
  nand.set_ecc_bits(4);
  nand.set_result(ZX_ERR_IO_DATA_INTEGRITY);
  op->rw.command = NAND_OP_READ;
  op->rw.length = 1;
  op->rw.offset_nand = 4;
  ASSERT_TRUE(operation.SetVmo());
  device()->NandQueue(op, &NandDeviceTest::CompletionCb, &operation);
  ASSERT_TRUE(Wait());
  ASSERT_OK(operation.status());

  expected.attempts[2] += 1;
  expected.internal_failure += 2;
  expected.ecc_bit_flips[ecc_limit + 1] += 2;
  expected.ecc_bit_flips[4] += 1;
  ASSERT_NO_FAILURES(ExpectMetricsMatch(expected, inspect_vmo));

  // Fails with unexpected reason before succeeding. Should not record bit flips for failures.
  nand.set_result(ZX_ERR_BAD_STATE);
  retries = 3;
  op->rw.command = NAND_OP_READ;
  op->rw.length = 1;
  op->rw.offset_nand = 5;
  ASSERT_TRUE(operation.SetVmo());
  device()->NandQueue(op, &NandDeviceTest::CompletionCb, &operation);
  ASSERT_TRUE(Wait());
  ASSERT_OK(operation.status());

  expected.attempts[2] += 1;
  expected.internal_failure += 2;
  expected.ecc_bit_flips[4] += 1;
  ASSERT_NO_FAILURES(ExpectMetricsMatch(expected, inspect_vmo));

  // Totally fails out on retries.
  nand.set_result(ZX_ERR_IO_DATA_INTEGRITY);
  retries = 1000000;
  op->rw.command = NAND_OP_READ;
  op->rw.length = 1;
  op->rw.offset_nand = 6;
  ASSERT_TRUE(operation.SetVmo());
  device()->NandQueue(op, &NandDeviceTest::CompletionCb, &operation);
  ASSERT_TRUE(Wait());
  ASSERT_NOT_OK(operation.status());

  expected.attempts_overflow += 1;
  expected.failure += 1;
  expected.internal_failure += nand::NandDevice::kNandReadRetries;
  expected.ecc_bit_flips[ecc_limit + 1] += nand::NandDevice::kNandReadRetries;
  ASSERT_NO_FAILURES(ExpectMetricsMatch(expected, inspect_vmo));
}

TEST_F(NandDeviceTest, ReadCacheForPoorECC) {
  Operation operation(op_size(), this);

  nand_operation_t* op = operation.GetOperation();
  ASSERT_NOT_NULL(op);

  // Normal read with no ECC errors. Should not cache.
  op->rw.command = NAND_OP_READ;
  op->rw.length = 1;
  op->rw.offset_nand = 3;
  ASSERT_TRUE(operation.SetVmo());
  device()->NandQueue(op, &NandDeviceTest::CompletionCb, &operation);
  ASSERT_TRUE(Wait());
  ASSERT_OK(operation.status());

  FakeRawNand& nand = raw_nand();

  // Read the same page with moderate ECC errors. Should not cache.
  nand.set_ecc_bits(1);
  op->rw.command = NAND_OP_READ;
  op->rw.length = 1;
  op->rw.offset_nand = 3;
  ASSERT_TRUE(operation.SetVmo());
  device()->NandQueue(op, &NandDeviceTest::CompletionCb, &operation);
  ASSERT_TRUE(Wait());
  ASSERT_OK(operation.status());

  nand_info_t info;
  ASSERT_OK(nand.RawNandGetNandInfo(&info));
  uint32_t ecc_limit = info.ecc_bits;

  // Read the same page with terrible ECC errors. Should do a normal read, but also should cache
  // for the next call.
  nand.set_ecc_bits(ecc_limit);
  op->rw.command = NAND_OP_READ;
  op->rw.length = 1;
  op->rw.offset_nand = 3;
  ASSERT_TRUE(operation.SetVmo());
  device()->NandQueue(op, &NandDeviceTest::CompletionCb, &operation);
  ASSERT_TRUE(Wait());
  ASSERT_OK(operation.status());
  // See all the bit flips.
  ASSERT_EQ(operation.GetOperation()->rw.corrected_bit_flips, ecc_limit);

  // Read the same page again. Should get the cached result.
  nand.set_ecc_bits(ecc_limit);
  op->rw.command = NAND_OP_READ;
  op->rw.length = 1;
  op->rw.offset_nand = 3;
  ASSERT_TRUE(operation.SetVmo());
  device()->NandQueue(op, &NandDeviceTest::CompletionCb, &operation);
  ASSERT_TRUE(Wait());
  ASSERT_OK(operation.status());
  // Cached result reports no bit flips.
  ASSERT_EQ(operation.GetOperation()->rw.corrected_bit_flips, 0);
}

TEST_F(NandDeviceTest, ReadCacheFailedRetry) {
  Operation operation(op_size(), this);

  nand_operation_t* op = operation.GetOperation();
  ASSERT_NOT_NULL(op);

  // Normal read with no ECC errors. Should not cache.
  op->rw.command = NAND_OP_READ;
  op->rw.length = 1;
  op->rw.offset_nand = 3;
  ASSERT_TRUE(operation.SetVmo());
  device()->NandQueue(op, &NandDeviceTest::CompletionCb, &operation);
  ASSERT_TRUE(Wait());
  ASSERT_OK(operation.status());

  FakeRawNand& nand = raw_nand();
  nand_info_t info;
  ASSERT_OK(nand.RawNandGetNandInfo(&info));
  size_t ecc_limit = info.ecc_bits;
  int retries = 2;
  nand.set_read_callback([&retries](FakeRawNand* n) {
    if (--retries == 0) {
      n->set_result(ZX_OK);
    }
  });

  // Read the same page. Fails ECC the first time before succeeding with max bit flips.
  nand.set_ecc_bits(1);
  nand.set_result(ZX_ERR_IO_DATA_INTEGRITY);
  op->rw.command = NAND_OP_READ;
  op->rw.length = 1;
  op->rw.offset_nand = 3;
  ASSERT_TRUE(operation.SetVmo());
  device()->NandQueue(op, &NandDeviceTest::CompletionCb, &operation);
  ASSERT_TRUE(Wait());
  ASSERT_OK(operation.status());
  ASSERT_EQ(operation.GetOperation()->rw.corrected_bit_flips, ecc_limit);

  // Read again. Prime for failures and bit flips again. It should return no bitflips this time due
  // to cache.
  nand.set_ecc_bits(1);
  retries = 2;
  nand.set_result(ZX_ERR_IO_DATA_INTEGRITY);
  op->rw.command = NAND_OP_READ;
  op->rw.length = 1;
  op->rw.offset_nand = 3;
  ASSERT_TRUE(operation.SetVmo());
  device()->NandQueue(op, &NandDeviceTest::CompletionCb, &operation);
  ASSERT_TRUE(Wait());
  ASSERT_OK(operation.status());
  ASSERT_EQ(operation.GetOperation()->rw.corrected_bit_flips, 0);
}

TEST_F(NandDeviceTest, ReadCachePurgeOnErase) {
  Operation operation(op_size(), this);

  nand_operation_t* op = operation.GetOperation();
  ASSERT_NOT_NULL(op);

  // Normal read with no ECC errors. Should not cache.
  op->rw.command = NAND_OP_READ;
  op->rw.length = 1;
  op->rw.offset_nand = 3;
  ASSERT_TRUE(operation.SetVmo());
  device()->NandQueue(op, &NandDeviceTest::CompletionCb, &operation);
  ASSERT_TRUE(Wait());
  ASSERT_OK(operation.status());

  FakeRawNand& nand = raw_nand();

  nand_info_t info;
  ASSERT_OK(nand.RawNandGetNandInfo(&info));
  uint32_t ecc_limit = info.ecc_bits;

  // Read the same page with terrible ECC errors. Should do a normal read, but also should cache
  // for the next call.
  nand.set_ecc_bits(ecc_limit);
  op->rw.command = NAND_OP_READ;
  op->rw.length = 1;
  op->rw.offset_nand = 3;
  ASSERT_TRUE(operation.SetVmo());
  device()->NandQueue(op, &NandDeviceTest::CompletionCb, &operation);
  ASSERT_TRUE(Wait());
  ASSERT_OK(operation.status());
  // See all the bit flips.
  ASSERT_EQ(operation.GetOperation()->rw.corrected_bit_flips, ecc_limit);

  // Read the same page again. Should get the cached result.
  nand.set_ecc_bits(ecc_limit);
  op->rw.command = NAND_OP_READ;
  op->rw.length = 1;
  op->rw.offset_nand = 3;
  ASSERT_TRUE(operation.SetVmo());
  device()->NandQueue(op, &NandDeviceTest::CompletionCb, &operation);
  ASSERT_TRUE(Wait());
  ASSERT_OK(operation.status());
  // Cached result reports no bit flips.
  ASSERT_EQ(operation.GetOperation()->rw.corrected_bit_flips, 0);

  // Purge from cache.
  Operation erase_operation(op_size(), this);
  nand_operation_t* erase_op = erase_operation.GetOperation();
  erase_op->command = NAND_OP_ERASE;
  erase_op->erase.command = NAND_OP_ERASE;
  erase_op->erase.first_block = 3 / info.pages_per_block;
  erase_op->erase.num_blocks = 1;
  device()->NandQueue(erase_op, &NandDeviceTest::CompletionCb, &erase_operation);
  ASSERT_TRUE(Wait());
  ASSERT_OK(erase_operation.status());

  // Get a full read without cached result, so we see bit flips.
  nand.set_ecc_bits(ecc_limit);
  op->rw.command = NAND_OP_READ;
  op->rw.length = 1;
  op->rw.offset_nand = 3;
  ASSERT_TRUE(operation.SetVmo());
  device()->NandQueue(op, &NandDeviceTest::CompletionCb, &operation);
  ASSERT_TRUE(Wait());
  ASSERT_OK(operation.status());
  // See all the bit flips.
  ASSERT_EQ(operation.GetOperation()->rw.corrected_bit_flips, ecc_limit);
}

}  // namespace
