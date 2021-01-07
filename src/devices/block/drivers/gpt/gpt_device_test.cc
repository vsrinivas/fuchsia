// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/hardware/block/c/banjo.h>
#include <fuchsia/hardware/block/cpp/banjo.h>
#include <fuchsia/hardware/block/partition/cpp/banjo.h>
#include <inttypes.h>
#include <lib/fake_ddk/fake_ddk.h>
#include <lib/zircon-internal/thread_annotations.h>
#include <lib/zx/vmo.h>
#include <zircon/device/block.h>
#include <zircon/errors.h>
#include <zircon/hw/gpt.h>

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddktl/device.h>
#include <fbl/alloc_checker.h>
#include <fbl/string.h>
#include <fbl/vector.h>
#include <gpt/c/gpt.h>
#include <zxtest/zxtest.h>

#include "gpt.h"
#include "gpt_test_data.h"

namespace gpt {
namespace {

// To make sure that we correctly convert UTF-16, to UTF-8, the second partition has a suffix with
// codepoint 0x10000, which in UTF-16 requires a surrogate pair.
constexpr std::string_view kPartition1Name = "Linux filesystem\xf0\x90\x80\x80";

class FakeBlockDevice : public ddk::BlockProtocol<FakeBlockDevice> {
 public:
  FakeBlockDevice() : proto_({&block_protocol_ops_, this}) {
    info_.block_count = kBlockCnt;
    info_.block_size = kBlockSz;
    info_.max_transfer_size = BLOCK_MAX_TRANSFER_UNBOUNDED;
  }

  block_protocol_t* proto() { return &proto_; }

  void SetInfo(const block_info_t* info) { info_ = *info; }

  void BlockQuery(block_info_t* info_out, size_t* block_op_size_out) {
    *info_out = info_;
    *block_op_size_out = sizeof(block_op_t);
  }

  void BlockQueue(block_op_t* operation, block_queue_callback completion_cb, void* cookie);

 private:
  zx_status_t BlockQueueOp(block_op_t* op);

  block_protocol_t proto_{};
  block_info_t info_{};
};

void FakeBlockDevice::BlockQueue(block_op_t* operation, block_queue_callback completion_cb,
                                 void* cookie) {
  zx_status_t status = BlockQueueOp(operation);
  completion_cb(cookie, status, operation);
}

zx_status_t FakeBlockDevice::BlockQueueOp(block_op_t* op) {
  const uint32_t command = op->command & BLOCK_OP_MASK;
  const uint32_t bsize = info_.block_size;
  if (command == BLOCK_OP_READ || command == BLOCK_OP_WRITE) {
    if ((op->rw.offset_dev + op->rw.length) > (bsize * info_.block_count)) {
      return ZX_ERR_OUT_OF_RANGE;
    }
    if (command == BLOCK_OP_WRITE) {
      return ZX_OK;
    }
  } else if (command == BLOCK_OP_TRIM) {
    if ((op->trim.offset_dev + op->trim.length) > (bsize * info_.block_count)) {
      return ZX_ERR_OUT_OF_RANGE;
    }
    return ZX_OK;
  } else if (command == BLOCK_OP_FLUSH) {
    return ZX_OK;
  } else {
    return ZX_ERR_NOT_SUPPORTED;
  }

  size_t part_size = sizeof(test_partition_table);
  size_t read_off = op->rw.offset_dev * bsize;
  size_t read_len = op->rw.length * bsize;

  if (read_len == 0) {
    return ZX_OK;
  }

  uint64_t vmo_addr = op->rw.offset_vmo * bsize;

  // Read initial part from header if in range.
  if (read_off < part_size) {
    size_t part_read_len = part_size - read_off;
    if (part_read_len > read_len) {
      part_read_len = read_len;
    }
    zx_vmo_write(op->rw.vmo, test_partition_table + read_off, vmo_addr, part_read_len);

    read_len -= part_read_len;
    read_off += part_read_len;
    vmo_addr += part_read_len;

    if (read_len == 0) {
      return ZX_OK;
    }
  }

  std::unique_ptr<uint8_t[]> zbuf(new uint8_t[bsize]);
  memset(zbuf.get(), 0, bsize);
  // Zero-fill remaining.
  for (; read_len > 0; read_len -= bsize) {
    zx_vmo_write(op->rw.vmo, zbuf.get(), vmo_addr, bsize);
    vmo_addr += bsize;
  }
  return ZX_OK;
}

class GptDeviceTest : public zxtest::Test {
 public:
  GptDeviceTest() = default;

  DISALLOW_COPY_ASSIGN_AND_MOVE(GptDeviceTest);

  void SetInfo(const block_info_t* info) { fake_block_device_.SetInfo(info); }

  void Init() {
    fbl::AllocChecker ac;
    fbl::Array<fake_ddk::ProtocolEntry> protocols(new (&ac) fake_ddk::ProtocolEntry[1](), 1);
    ASSERT_TRUE(ac.check());
    protocols[0] = {ZX_PROTOCOL_BLOCK,
                    {fake_block_device_.proto()->ops, fake_block_device_.proto()->ctx}};
    ddk_.SetProtocols(std::move(protocols));
  }

  fake_ddk::Bind ddk_;

 protected:
  struct BlockOpResult {
    sync_completion_t completion;
    block_op_t op;
    zx_status_t status;
  };

  static void BlockOpCompleter(void* cookie, zx_status_t status, block_op_t* bop) {
    auto* result = static_cast<BlockOpResult*>(cookie);
    result->status = status;
    result->op = *bop;
    sync_completion_signal(&result->completion);
  }

 private:
  FakeBlockDevice fake_block_device_;
};

TEST_F(GptDeviceTest, DeviceTooSmall) {
  Init();

  const block_info_t info = {20, 512, BLOCK_MAX_TRANSFER_UNBOUNDED, 0, 0};
  SetInfo(&info);

  TableRef tab;
  ASSERT_OK(PartitionTable::Create(fake_ddk::kFakeParent, &tab));
  ASSERT_EQ(ZX_ERR_NO_SPACE, tab->Bind());
}

TEST_F(GptDeviceTest, DdkLifecycle) {
  Init();
  fbl::Vector<std::unique_ptr<PartitionDevice>> devices;

  TableRef tab;
  ASSERT_OK(PartitionTable::Create(fake_ddk::kFakeParent, &tab, &devices));
  ASSERT_OK(tab->Bind());

  ASSERT_EQ(devices.size(), 2);

  char name[MAX_PARTITION_NAME_LENGTH];
  guid_t guid;

  // Device 0
  PartitionDevice* dev0 = devices[0].get();
  ASSERT_NOT_NULL(dev0);
  ASSERT_OK(dev0->BlockPartitionGetName(name, sizeof(name)));
  ASSERT_EQ(strcmp(name, "Linux filesystem"), 0);
  ASSERT_OK(dev0->BlockPartitionGetGuid(GUIDTYPE_TYPE, &guid));
  {
    uint8_t expected_guid[GPT_GUID_LEN] = GUID_LINUX_FILESYSTEM;
    EXPECT_BYTES_EQ(reinterpret_cast<uint8_t*>(&guid), expected_guid, GPT_GUID_LEN);
  }
  ASSERT_OK(dev0->BlockPartitionGetGuid(GUIDTYPE_INSTANCE, &guid));
  {
    uint8_t expected_guid[GPT_GUID_LEN] = GUID_UNIQUE_PART0;
    EXPECT_BYTES_EQ(reinterpret_cast<uint8_t*>(&guid), expected_guid, GPT_GUID_LEN);
  }

  PartitionDevice* dev1 = devices[1].get();
  ASSERT_NOT_NULL(dev1);
  ASSERT_OK(dev1->BlockPartitionGetName(name, sizeof(name)));
  ASSERT_EQ(kPartition1Name, name);

  ASSERT_OK(dev1->BlockPartitionGetGuid(GUIDTYPE_TYPE, &guid));
  {
    uint8_t expected_guid[GPT_GUID_LEN] = GUID_LINUX_FILESYSTEM;
    EXPECT_BYTES_EQ(reinterpret_cast<uint8_t*>(&guid), expected_guid, GPT_GUID_LEN);
  }
  ASSERT_OK(dev1->BlockPartitionGetGuid(GUIDTYPE_INSTANCE, &guid));
  {
    uint8_t expected_guid[GPT_GUID_LEN] = GUID_UNIQUE_PART1;
    EXPECT_BYTES_EQ(reinterpret_cast<uint8_t*>(&guid), expected_guid, GPT_GUID_LEN);
  }

  dev0->AsyncRemove();
  dev1->AsyncRemove();

  EXPECT_TRUE(ddk_.Ok());
}

TEST_F(GptDeviceTest, GuidMapMetadata) {
  Init();
  fbl::Vector<std::unique_ptr<PartitionDevice>> devices;

  const guid_map_t guid_map[] = {
      {"Linux filesystem", GUID_METADATA},
  };
  ddk_.SetMetadata(&guid_map, sizeof(guid_map));

  TableRef tab;
  ASSERT_OK(PartitionTable::Create(fake_ddk::kFakeParent, &tab, &devices));
  ASSERT_OK(tab->Bind());

  ASSERT_EQ(devices.size(), 2);

  char name[MAX_PARTITION_NAME_LENGTH];
  guid_t guid;

  // Device 0
  PartitionDevice* dev0 = devices[0].get();
  ASSERT_NOT_NULL(dev0);
  ASSERT_OK(dev0->BlockPartitionGetName(name, sizeof(name)));
  ASSERT_EQ(strcmp(name, "Linux filesystem"), 0);
  ASSERT_OK(dev0->BlockPartitionGetGuid(GUIDTYPE_TYPE, &guid));
  {
    uint8_t expected_guid[GPT_GUID_LEN] = GUID_METADATA;
    EXPECT_BYTES_EQ(reinterpret_cast<uint8_t*>(&guid), expected_guid, GPT_GUID_LEN);
  }
  ASSERT_OK(dev0->BlockPartitionGetGuid(GUIDTYPE_INSTANCE, &guid));
  {
    uint8_t expected_guid[GPT_GUID_LEN] = GUID_UNIQUE_PART0;
    EXPECT_BYTES_EQ(reinterpret_cast<uint8_t*>(&guid), expected_guid, GPT_GUID_LEN);
  }

  PartitionDevice* dev1 = devices[1].get();
  ASSERT_NOT_NULL(dev1);
  ASSERT_OK(dev1->BlockPartitionGetName(name, sizeof(name)));
  ASSERT_EQ(kPartition1Name, name);

  ASSERT_OK(dev1->BlockPartitionGetGuid(GUIDTYPE_TYPE, &guid));
  {
    uint8_t expected_guid[GPT_GUID_LEN] = GUID_LINUX_FILESYSTEM;
    EXPECT_BYTES_EQ(reinterpret_cast<uint8_t*>(&guid), expected_guid, GPT_GUID_LEN);
  }
  ASSERT_OK(dev1->BlockPartitionGetGuid(GUIDTYPE_INSTANCE, &guid));
  {
    uint8_t expected_guid[GPT_GUID_LEN] = GUID_UNIQUE_PART1;
    EXPECT_BYTES_EQ(reinterpret_cast<uint8_t*>(&guid), expected_guid, GPT_GUID_LEN);
  }

  dev0->AsyncRemove();
  dev1->AsyncRemove();

  EXPECT_TRUE(ddk_.Ok());
}

TEST_F(GptDeviceTest, BlockOpsPropagate) {
  Init();
  fbl::Vector<std::unique_ptr<PartitionDevice>> devices;

  const guid_map_t guid_map[] = {
      {"Linux filesystem", GUID_METADATA},
  };
  ddk_.SetMetadata(&guid_map, sizeof(guid_map));

  TableRef tab;
  ASSERT_OK(PartitionTable::Create(fake_ddk::kFakeParent, &tab, &devices));
  ASSERT_OK(tab->Bind());

  ASSERT_EQ(devices.size(), 2);

  PartitionDevice* dev0 = devices[0].get();
  PartitionDevice* dev1 = devices[1].get();

  block_info_t block_info = {};
  size_t block_op_size = 0;
  dev0->BlockImplQuery(&block_info, &block_op_size);
  EXPECT_EQ(block_op_size, sizeof(block_op_t));

  zx::vmo vmo;
  ASSERT_OK(zx::vmo::create(4 * block_info.block_size, 0, &vmo));

  block_op_t op = {};
  op.rw.command = BLOCK_OP_READ;
  op.rw.vmo = vmo.get();
  op.rw.length = 4;
  op.rw.offset_dev = 1000;

  BlockOpResult result;
  dev0->BlockImplQueue(&op, BlockOpCompleter, &result);
  sync_completion_wait(&result.completion, ZX_TIME_INFINITE);
  sync_completion_reset(&result.completion);

  EXPECT_EQ(result.op.command, BLOCK_OP_READ);
  EXPECT_EQ(result.op.rw.length, 4);
  EXPECT_EQ(result.op.rw.offset_dev, 2048 + 1000);
  EXPECT_OK(result.status);

  op.rw.command = BLOCK_OP_WRITE;
  op.rw.vmo = vmo.get();
  op.rw.length = 4;
  op.rw.offset_dev = 5000;

  dev1->BlockImplQueue(&op, BlockOpCompleter, &result);
  sync_completion_wait(&result.completion, ZX_TIME_INFINITE);
  sync_completion_reset(&result.completion);

  EXPECT_EQ(result.op.command, BLOCK_OP_WRITE);
  EXPECT_EQ(result.op.rw.length, 4);
  EXPECT_EQ(result.op.rw.offset_dev, 22528 + 5000);
  EXPECT_OK(result.status);

  op.trim.command = BLOCK_OP_TRIM;
  op.trim.length = 16;
  op.trim.offset_dev = 10000;

  dev0->BlockImplQueue(&op, BlockOpCompleter, &result);
  sync_completion_wait(&result.completion, ZX_TIME_INFINITE);
  sync_completion_reset(&result.completion);

  EXPECT_EQ(result.op.command, BLOCK_OP_TRIM);
  EXPECT_EQ(result.op.trim.length, 16);
  EXPECT_EQ(result.op.trim.offset_dev, 2048 + 10000);
  EXPECT_OK(result.status);

  op.command = BLOCK_OP_FLUSH;

  dev1->BlockImplQueue(&op, BlockOpCompleter, &result);
  sync_completion_wait(&result.completion, ZX_TIME_INFINITE);
  sync_completion_reset(&result.completion);

  EXPECT_EQ(result.op.command, BLOCK_OP_FLUSH);
  EXPECT_OK(result.status);

  dev0->AsyncRemove();
  dev1->AsyncRemove();

  EXPECT_TRUE(ddk_.Ok());
}

TEST_F(GptDeviceTest, BlockOpsOutOfBounds) {
  Init();
  fbl::Vector<std::unique_ptr<PartitionDevice>> devices;

  const guid_map_t guid_map[] = {
      {"Linux filesystem", GUID_METADATA},
  };
  ddk_.SetMetadata(&guid_map, sizeof(guid_map));

  TableRef tab;
  ASSERT_OK(PartitionTable::Create(fake_ddk::kFakeParent, &tab, &devices));
  ASSERT_OK(tab->Bind());

  ASSERT_EQ(devices.size(), 2);

  PartitionDevice* dev0 = devices[0].get();
  PartitionDevice* dev1 = devices[1].get();

  block_info_t block_info = {};
  size_t block_op_size = 0;
  dev0->BlockImplQuery(&block_info, &block_op_size);
  EXPECT_EQ(block_op_size, sizeof(block_op_t));

  zx::vmo vmo;
  ASSERT_OK(zx::vmo::create(4 * block_info.block_size, 0, &vmo));

  block_op_t op = {};
  op.rw.command = BLOCK_OP_READ;
  op.rw.vmo = vmo.get();
  op.rw.length = 4;
  op.rw.offset_dev = 20481;

  BlockOpResult result;
  dev0->BlockImplQueue(&op, BlockOpCompleter, &result);
  sync_completion_wait(&result.completion, ZX_TIME_INFINITE);
  sync_completion_reset(&result.completion);

  EXPECT_NOT_OK(result.status);

  op.rw.command = BLOCK_OP_WRITE;
  op.rw.vmo = vmo.get();
  op.rw.length = 4;
  op.rw.offset_dev = 20478;

  dev0->BlockImplQueue(&op, BlockOpCompleter, &result);
  sync_completion_wait(&result.completion, ZX_TIME_INFINITE);
  sync_completion_reset(&result.completion);

  EXPECT_NOT_OK(result.status);

  op.trim.command = BLOCK_OP_TRIM;
  op.trim.length = 18434;
  op.trim.offset_dev = 0;

  dev1->BlockImplQueue(&op, BlockOpCompleter, &result);
  sync_completion_wait(&result.completion, ZX_TIME_INFINITE);
  sync_completion_reset(&result.completion);

  EXPECT_NOT_OK(result.status);

  dev0->AsyncRemove();
  dev1->AsyncRemove();

  EXPECT_TRUE(ddk_.Ok());
}

}  // namespace
}  // namespace gpt
