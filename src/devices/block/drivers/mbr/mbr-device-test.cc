// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "mbr-device.h"

#include <fuchsia/hardware/block/c/banjo.h>
#include <inttypes.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/device.h>
#include <lib/ddk/driver.h>
#include <lib/zircon-internal/thread_annotations.h>
#include <zircon/errors.h>
#include <zircon/hw/gpt.h>

#include <fbl/alloc_checker.h>
#include <fbl/string.h>
#include <fbl/vector.h>
#include <gpt/c/gpt.h>
#include <zxtest/zxtest.h>

#include "mbr-test-data.h"
#include "mbr.h"
#include "src/devices/testing/mock-ddk/mock-device.h"

namespace {

constexpr uint32_t kBlockSz = 512;
constexpr uint32_t kBlockCnt = 20;
const block_info_t kInfo = {kBlockCnt, kBlockSz, BLOCK_MAX_TRANSFER_UNBOUNDED, 0, 0};

class FakeBlockDevice : public ddk::BlockProtocol<FakeBlockDevice> {
 public:
  FakeBlockDevice() : mbr_{kFuchsiaMbr} {}

  block_protocol_ops_t* proto_ops() { return &block_protocol_ops_; }

  void BlockQuery(block_info_t* info_out, size_t* block_op_size_out) {
    *info_out = kInfo;
    *block_op_size_out = sizeof(block_op_t);
  }

  void BlockQueue(block_op_t* operation, block_queue_callback completion_cb, void* cookie) {
    if (operation->rw.command == BLOCK_OP_READ) {
      if ((operation->rw.offset_dev + operation->rw.length) * kBlockSz <= mbr::kMbrSize) {
        // Reading from header
        uint64_t vmo_addr = operation->rw.offset_vmo * kBlockSz;
        off_t off = operation->rw.offset_dev * kBlockSz;
        zx_vmo_write(operation->rw.vmo, mbr_ + off, vmo_addr, operation->rw.length * kBlockSz);
      } else {
      }
    } else if (operation->rw.command == BLOCK_OP_WRITE) {
      // Ensure the header is never written into.
      ASSERT_GT((operation->rw.offset_dev + operation->rw.length) * kBlockSz, mbr::kMbrSize);
    }
    completion_cb(cookie, ZX_OK, operation);
  }

  void SetMbr(const uint8_t* new_mbr) { mbr_ = new_mbr; }

 private:
  const uint8_t* mbr_;
};

}  // namespace

namespace mbr {

class MbrDeviceTest : public zxtest::Test {
 public:
  MbrDeviceTest() {}

  void Init() {
    fake_parent_->AddProtocol(ZX_PROTOCOL_BLOCK, fake_block_device_.proto_ops(),
                              &fake_block_device_);
  }

  DISALLOW_COPY_ASSIGN_AND_MOVE(MbrDeviceTest);

  std::shared_ptr<MockDevice> fake_parent_ = MockDevice::FakeRootParent();

 protected:
  FakeBlockDevice fake_block_device_;
};

TEST_F(MbrDeviceTest, DeviceCreation) {
  Init();

  fbl::Vector<std::unique_ptr<MbrDevice>> devices;
  ASSERT_OK(MbrDevice::Create(fake_parent_.get(), &devices));
  ASSERT_EQ(devices.size(), 2);

  ASSERT_NOT_NULL(devices[0].get());
  EXPECT_STREQ(devices[0]->Name().c_str(), "part-000");
  guid_t guid;
  EXPECT_OK(devices[0]->BlockPartitionGetGuid(GUIDTYPE_TYPE, &guid));
  {
    uint8_t expected_guid[GPT_GUID_LEN] = GUID_SYSTEM_VALUE;
    EXPECT_BYTES_EQ(reinterpret_cast<uint8_t*>(&guid), expected_guid, GPT_GUID_LEN);
  }

  ASSERT_NOT_NULL(devices[1].get());
  EXPECT_STREQ(devices[1]->Name().c_str(), "part-001");
  EXPECT_OK(devices[1]->BlockPartitionGetGuid(GUIDTYPE_TYPE, &guid));
  {
    uint8_t expected_guid[GPT_GUID_LEN] = GUID_DATA_VALUE;
    EXPECT_BYTES_EQ(reinterpret_cast<uint8_t*>(&guid), expected_guid, GPT_GUID_LEN);
  }
}

TEST_F(MbrDeviceTest, DeviceCreationProtectiveMbr) {
  fake_block_device_.SetMbr(kProtectiveMbr);
  Init();

  fbl::Vector<std::unique_ptr<MbrDevice>> devices;
  ASSERT_EQ(MbrDevice::Create(fake_parent_.get(), &devices), ZX_ERR_NOT_SUPPORTED);
}

TEST_F(MbrDeviceTest, DdkLifecycle) {
  Init();

  fbl::Vector<std::unique_ptr<MbrDevice>> devices;
  ASSERT_OK(MbrDevice::Create(fake_parent_.get(), &devices));
  ASSERT_EQ(devices.size(), 2);

  auto* device0 = devices[0].get();
  ASSERT_NOT_NULL(device0);
  EXPECT_OK(MbrDevice::Bind(std::move(devices[0])));

  auto* device1 = devices[1].get();
  ASSERT_NOT_NULL(device1);
  EXPECT_OK(MbrDevice::Bind(std::move(devices[1])));

  // TODO(fxbug.dev/79639): Removed the fake_ddk.Ok() check, which did
  // not actually test lifecycle. Implement any checks necessary to test
  // that this driver implements Unbind and Release properly, if needed.
}

TEST(Bind, UnsupportedProtocol) {
  std::shared_ptr<MockDevice> fake_parent = MockDevice::FakeRootParent();

  auto bind_result = MbrDriverOps.bind(nullptr, fake_parent.get());
  ASSERT_EQ(bind_result, ZX_ERR_NOT_SUPPORTED);
}

}  // namespace mbr
