// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "block_device.h"

#include <fuchsia/hardware/block/c/banjo.h>
#include <fuchsia/hardware/nand/cpp/banjo.h>
#include <lib/fit/function.h>
#include <lib/ftl/volume.h>
#include <lib/fzl/owned-vmo-mapper.h>
#include <lib/inspect/cpp/reader.h>
#include <lib/inspect/cpp/vmo/types.h>

#include <atomic>
#include <memory>
#include <utility>

#include <fbl/array.h>
#include <zxtest/zxtest.h>

#include "metrics.h"
#include "src/devices/testing/mock-ddk/mock-device.h"
namespace {

constexpr uint32_t kPageSize = 1024;
constexpr uint32_t kNumPages = 20;
constexpr char kMagic = 'f';
constexpr uint8_t kGuid[ZBI_PARTITION_GUID_LEN] = {'g', 'u', 'i', 'd'};
constexpr uint32_t kWearCount = 1337;
constexpr uint32_t kInitialBadBlocks = 3;
constexpr uint32_t kRunningBadBlocks = 4;

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
  void NandQuery(nand_info_t* out_info, size_t* out_nand_op_size) {
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
    OnOperation();
    first_page_ = first_page;
    num_pages_ = num_pages;
    memset(buffer, kMagic, num_pages * kPageSize);
    return ZX_OK;
  }
  zx_status_t Write(uint32_t first_page, int num_pages, const void* buffer) final {
    OnOperation();
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
    OnOperation();
    flushed_ = true;
    return ZX_OK;
  }
  zx_status_t Trim(uint32_t first_page, uint32_t num_pages) final {
    OnOperation();
    trimmed_ = true;
    first_page_ = first_page;
    num_pages_ = num_pages;
    return ZX_OK;
  }

  zx_status_t GarbageCollect() final { return ZX_OK; }

  zx_status_t GetStats(Stats* stats) final {
    *stats = {};
    stats->wear_count = wear_count_;
    stats->initial_bad_blocks = initial_bad_blocks_;
    stats->running_bad_blocks = running_bad_blocks_;
    return ZX_OK;
  }

  zx_status_t GetCounters(Counters* counters) final {
    counters->wear_count = wear_count_;
    counters->initial_bad_blocks = initial_bad_blocks_;
    counters->running_bad_blocks = running_bad_blocks_;
    return ZX_OK;
  }

  void UpdateWearCount(uint32_t wear_count) { wear_count_ = wear_count; }

  void UpdateInitialBadBlockCount(uint32_t initial_bad_blocks) {
    initial_bad_blocks_ = initial_bad_blocks;
  }
  void UpdateRunningBadBlockCount(uint32_t running_bad_blocks) {
    running_bad_blocks_ = running_bad_blocks;
  }

  void SetOnOperation(fit::function<void()> callback) { on_operation_ = std::move(callback); }

 private:
  void OnOperation() {
    if (on_operation_) {
      on_operation_();
    }
  }

  ftl::BlockDevice* device_;
  uint32_t first_page_ = 0;
  int num_pages_ = 0;
  uint32_t wear_count_ = kWearCount;
  uint32_t initial_bad_blocks_ = kInitialBadBlocks;
  uint32_t running_bad_blocks_ = kRunningBadBlocks;
  fit::function<void()> on_operation_;
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
  std::shared_ptr<MockDevice> fake_parent = MockDevice::FakeRootParent();
  ftl::BlockDevice* device(new ftl::BlockDevice(fake_parent.get()));
  device->SetVolumeForTest(std::make_unique<FakeVolume>(device));

  FakeNand nand;
  fake_parent->AddProtocol(ZX_PROTOCOL_NAND, nand.proto()->ops, nand.proto()->ctx);
  ASSERT_OK(device->Bind());
  device->DdkAsyncRemove();
  ASSERT_OK(mock_ddk::ReleaseFlaggedDevices(fake_parent.get()));
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

  void Read() {
    Operation operation(op_size(), this);
    ASSERT_TRUE(operation.SetVmo());
    auto* op = operation.GetOperation();
    op->rw.command = BLOCK_OP_READ;
    op->rw.length = 1;
    op->rw.offset_dev = 0;
    device_->BlockImplQueue(op, &BlockDeviceTest::CompletionCb, &operation);

    ASSERT_TRUE(Wait());
    ASSERT_OK(operation.status());
  }

  void Write() {
    Operation operation(op_size(), this);
    ASSERT_TRUE(operation.SetVmo());
    auto* op = operation.GetOperation();
    op->rw.command = BLOCK_OP_WRITE;
    op->rw.length = 1;
    op->rw.offset_dev = 0;
    memset(operation.buffer(), kMagic, kPageSize);
    device_->BlockImplQueue(op, &BlockDeviceTest::CompletionCb, &operation);

    ASSERT_TRUE(Wait());
    ASSERT_OK(operation.status());
  }

  void Flush() {
    Operation operation(op_size(), this);
    ASSERT_TRUE(operation.SetVmo());
    auto* op = operation.GetOperation();
    op->rw.command = BLOCK_OP_FLUSH;
    device_->BlockImplQueue(op, &BlockDeviceTest::CompletionCb, &operation);

    ASSERT_TRUE(Wait());
    ASSERT_OK(operation.status());
  }

  void Trim() {
    Operation operation(op_size(), this);
    ASSERT_TRUE(operation.SetVmo());
    auto* op = operation.GetOperation();
    op->trim.command = BLOCK_OP_TRIM;
    op->trim.length = 1;
    op->trim.offset_dev = kNumPages - 1;
    device_->BlockImplQueue(op, &BlockDeviceTest::CompletionCb, &operation);

    ASSERT_TRUE(Wait());
    ASSERT_OK(operation.status());
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

  EXPECT_OK(device->FormatInternal());
  EXPECT_TRUE(GetVolume()->formatted());
  EXPECT_FALSE(GetVolume()->leveled());
}

TEST_F(BlockDeviceTest, GetInspectVmoContainsCountersAndWearCount) {
  ftl::BlockDevice* device = GetDevice();
  ASSERT_TRUE(device);

  zx::vmo vmo = device->DuplicateInspectVmo();
  auto base_hierarchy = inspect::ReadFromVmo(vmo).take_value();
  auto* hierarchy = base_hierarchy.GetByPath({"ftl"});
  ASSERT_NOT_NULL(hierarchy);
  for (const auto& property_name : ftl::Metrics::GetPropertyNames<inspect::UintProperty>()) {
    auto* property = hierarchy->node().get_property<inspect::UintPropertyValue>(property_name);
    EXPECT_NOT_NULL(property, "Missing Inspect Property: %s", property_name.c_str());
  }

  for (const auto& property_name : ftl::Metrics::GetPropertyNames<inspect::DoubleProperty>()) {
    auto* property = hierarchy->node().get_property<inspect::DoublePropertyValue>(property_name);
    EXPECT_NOT_NULL(property, "Missing Inspect Property: %s", property_name.c_str());
  }
}

void ReadProperties(ftl::BlockDevice* device, std::map<std::string, uint64_t>& counters,
                    std::map<std::string, double>& rates) {
  zx::vmo vmo = device->DuplicateInspectVmo();
  auto base_hierarchy = inspect::ReadFromVmo(vmo).take_value();
  auto* hierarchy = base_hierarchy.GetByPath({"ftl"});
  // counters are still 0.
  for (const auto& property_name : ftl::Metrics::GetPropertyNames<inspect::UintProperty>()) {
    auto* property = hierarchy->node().get_property<inspect::UintPropertyValue>(property_name);
    ASSERT_NOT_NULL(property, "Missing Inspect Property: %s", property_name.c_str());
    counters[property_name] = property->value();
  }

  for (const auto& property_name : ftl::Metrics::GetPropertyNames<inspect::DoubleProperty>()) {
    auto* property = hierarchy->node().get_property<inspect::DoublePropertyValue>(property_name);
    ASSERT_NOT_NULL(property, "Missing Inspect Property: %s", property_name.c_str());
    rates[property_name] = property->value();
  }
}

void VerifyInspectMetrics(BlockDeviceTest* fixture, const std::string& block_metric_prefix,
                          fit::function<std::string()> clear_op,
                          fit::function<void()> trigger_metric_update_op) {
  ftl::BlockDevice* device = fixture->GetDevice();
  ASSERT_TRUE(device);
  auto* volume = fixture->GetVolume();

  std::map<std::string, uint64_t> counters;
  std::map<std::string, double> rates;
  std::map<std::string, uint64_t> expected_counters;
  std::map<std::string, double> expected_rates;

  volume->UpdateWearCount(0);
  volume->UpdateInitialBadBlockCount(0);
  volume->UpdateRunningBadBlockCount(0);
  // Random operation to trigger a metric update.
  expected_counters[clear_op()]++;

  ReadProperties(device, counters, rates);
  for (const auto& counter : counters) {
    EXPECT_EQ(counter.second, expected_counters[counter.first],
              "Property %s had initial non zero counter.", counter.first.c_str());
  }

  // The counters are cleared before any operation.
  volume->SetOnOperation([&]() {
    auto& counters = device->nand_counters();
    counters.page_read = 1;
    counters.page_write = 2;
    counters.block_erase = 3;
  });

  volume->UpdateWearCount(24);
  trigger_metric_update_op();

  volume->SetOnOperation([&]() {
    auto& counters = device->nand_counters();
    counters.page_read = 2;
    counters.page_write = 4;
    counters.block_erase = 5;
  });
  volume->UpdateWearCount(12345678);
  trigger_metric_update_op();

  expected_counters[ftl::Metrics::GetMaxWearPropertyName()] = 12345678;
  expected_counters["nand.erase_block.max_wear"] = 12345678;

  // Counters
  expected_counters[block_metric_prefix + ".count"] = 2;
  expected_counters[block_metric_prefix + ".issued_nand_operation.count"] = 17;
  expected_counters[block_metric_prefix + ".issued_page_read.count"] = 3;
  expected_counters[block_metric_prefix + ".issued_page_write.count"] = 6;
  expected_counters[block_metric_prefix + ".issued_block_erase.count"] = 8;

  // Rates
  expected_rates[block_metric_prefix + ".issued_nand_operation.average_rate"] = 8.5;
  expected_rates[block_metric_prefix + ".issued_page_read.average_rate"] = 1.5;
  expected_rates[block_metric_prefix + ".issued_page_write.average_rate"] = 3;
  expected_rates[block_metric_prefix + ".issued_block_erase.average_rate"] = 4;

  ReadProperties(device, counters, rates);

  for (const auto& counter : counters) {
    EXPECT_EQ(counter.second, expected_counters[counter.first], "Property %s mismatch.",
              counter.first.c_str());
  }

  for (const auto& rate : rates) {
    EXPECT_EQ(rate.second, expected_rates[rate.first], "Property %s mismatch.", rate.first.c_str());
  }
}

TEST_F(BlockDeviceTest, InspectReadMetricsUpdatedCorrectly) {
  VerifyInspectMetrics(
      this, "block.read",
      [&]() {
        Flush();
        return "block.flush.count";
      },
      [&]() { Read(); });
}

TEST_F(BlockDeviceTest, InspectWriteMetricsUpdatedCorrectly) {
  VerifyInspectMetrics(
      this, "block.write",
      [&]() {
        Flush();
        return "block.flush.count";
      },
      [&]() { Write(); });
}

TEST_F(BlockDeviceTest, InspectTrimMetricsUpdatedCorrectly) {
  VerifyInspectMetrics(
      this, "block.trim",
      [&]() {
        Flush();
        return "block.flush.count";
      },
      [&]() { Trim(); });
}

TEST_F(BlockDeviceTest, InspectFlushMetricsUpdatedCorrectly) {
  VerifyInspectMetrics(
      this, "block.flush",
      [&]() {
        Trim();
        return "block.trim.count";
      },
      [&]() { Flush(); });
}

TEST_F(BlockDeviceTest, InspectBadBlockMetricsPopulation) {
  ftl::BlockDevice* device = GetDevice();
  ASSERT_TRUE(device);

  std::map<std::string, uint64_t> counters;
  std::map<std::string, double> rates;

  ReadProperties(device, counters, rates);
  ASSERT_EQ(counters["nand.initial_bad_blocks"], kInitialBadBlocks);
  ASSERT_EQ(counters["nand.running_bad_blocks"], kRunningBadBlocks);

  GetVolume()->UpdateInitialBadBlockCount(7);
  GetVolume()->UpdateRunningBadBlockCount(8);

  // Force a stats update.
  Read();

  ReadProperties(device, counters, rates);
  ASSERT_EQ(counters["nand.initial_bad_blocks"], 7);
  ASSERT_EQ(counters["nand.running_bad_blocks"], 8);
}

TEST_F(BlockDeviceTest, Suspend) {
  ftl::BlockDevice* device = GetDevice();
  ASSERT_TRUE(device);
  device->Suspend();
  EXPECT_TRUE(GetVolume()->flushed());
}

}  // namespace
