// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <inttypes.h>
#include <lib/fake_ddk/fake_ddk.h>
#include <lib/zircon-internal/thread_annotations.h>
#include <zircon/device/block.h>
#include <zircon/errors.h>
#include <zircon/hw/gpt.h>

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/protocol/block.h>
#include <ddktl/device.h>
#include <ddktl/protocol/block.h>
#include <ddktl/protocol/block/partition.h>
#include <fbl/alloc_checker.h>
#include <fbl/string.h>
#include <fbl/vector.h>
#include <gpt/c/gpt.h>
#include <zxtest/zxtest.h>

#include "gpt.h"
#include "gpt_test_data.h"

namespace {

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
  if (op->rw.command != BLOCK_OP_READ) {
    return ZX_ERR_NOT_SUPPORTED;
  }
  const uint32_t bsize = info_.block_size;
  if ((op->rw.offset_dev + op->rw.length) > (bsize * info_.block_count)) {
    return ZX_ERR_OUT_OF_RANGE;
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

}  // namespace

namespace gpt {

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

 private:
  FakeBlockDevice fake_block_device_;
};

TEST_F(GptDeviceTest, DeviceTooSmall) {
  Init();

  const block_info_t info = {20, 512, BLOCK_MAX_TRANSFER_UNBOUNDED, 0, 0};
  SetInfo(&info);

  TableRef tab;
  ASSERT_OK(PartitionTable::Create(fake_ddk::kFakeParent, &tab));
  ASSERT_NOT_OK(tab->Bind());
}

TEST_F(GptDeviceTest, DdkLifecycle) {
  Init();
  fbl::Vector<PartitionDevice*> devices;

  TableRef tab;
  ASSERT_OK(PartitionTable::Create(fake_ddk::kFakeParent, &tab, &devices));
  ASSERT_OK(tab->Bind());

  ASSERT_EQ(devices.size(), 2);

  char name[MAX_PARTITION_NAME_LENGTH];
  guid_t guid;

  // Device 0
  PartitionDevice* dev0 = devices[0];
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

  PartitionDevice* dev1 = devices[1];
  ASSERT_NOT_NULL(dev1);
  ASSERT_OK(dev1->BlockPartitionGetName(name, sizeof(name)));
  ASSERT_EQ(strcmp(name, "Linux filesystem"), 0);

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
  fbl::Vector<PartitionDevice*> devices;

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
  PartitionDevice* dev0 = devices[0];
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

  PartitionDevice* dev1 = devices[1];
  ASSERT_NOT_NULL(dev1);
  ASSERT_OK(dev1->BlockPartitionGetName(name, sizeof(name)));
  ASSERT_EQ(strcmp(name, "Linux filesystem"), 0);

  ASSERT_OK(dev1->BlockPartitionGetGuid(GUIDTYPE_TYPE, &guid));
  {
    uint8_t expected_guid[GPT_GUID_LEN] = GUID_METADATA;
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

}  // namespace gpt
