// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "scsi.h"

#include <lib/fake_ddk/fake_ddk.h>
#include <lib/virtio/backends/fake.h>

#include <memory>

#include <zxtest/zxtest.h>

using Queue = virtio::ScsiDevice::Queue;

namespace {

// Fake virtio 'backend' for a virtio-scsi device.
class FakeBackendForScsi : public virtio::FakeBackend {
 public:
  FakeBackendForScsi()
      : virtio::FakeBackend(
            /*queue_sizes=*/{{Queue::CONTROL, 128}, {Queue::REQUEST, 128}, {Queue::EVENT, 128}}) {
    // TODO(venkateshs): Sane defaults for these registers.
    AddClassRegister(offsetof(virtio_scsi_config, num_queues), 1);
    AddClassRegister(offsetof(virtio_scsi_config, seg_max), 1);
    AddClassRegister(offsetof(virtio_scsi_config, max_sectors), 1);
    AddClassRegister(offsetof(virtio_scsi_config, cmd_per_lun), 1);
    AddClassRegister(offsetof(virtio_scsi_config, event_info_size), 1);
    AddClassRegister(offsetof(virtio_scsi_config, sense_size), 1);
    AddClassRegister(offsetof(virtio_scsi_config, cdb_size), 1);
    AddClassRegister(offsetof(virtio_scsi_config, max_channel), static_cast<uint16_t>(1));
    AddClassRegister(offsetof(virtio_scsi_config, max_target), static_cast<uint16_t>(1));
    AddClassRegister(offsetof(virtio_scsi_config, max_lun), 1);
  }
};

TEST(ScsiTest, Init) {
  std::unique_ptr<virtio::Backend> backend = std::make_unique<FakeBackendForScsi>();
  zx::bti bti(ZX_HANDLE_INVALID);

  virtio::ScsiDevice scsi(/*parent=*/nullptr, std::move(bti), std::move(backend));
  auto status = scsi.Init();
  EXPECT_NE(status, ZX_OK);
}

TEST(ScsiTest, EncodeLun) {
  // Test that the virtio-scsi device correctly encodes single-level LUN structures.

  // Test encoding of target=1, LUN=1.
  struct virtio_scsi_req_cmd req = {};
  virtio::ScsiDevice::FillLUNStructure(&req, /*target=*/1, /*lun=*/1);
  EXPECT_EQ(req.lun[0], 1);
  EXPECT_EQ(req.lun[1], 1);
  // Expect flat addressing, single-level LUN structure.
  EXPECT_EQ(req.lun[2], 0x40 | 0x0);
  EXPECT_EQ(req.lun[3], 0x1);

  memset(&req, 0, sizeof(req));

  // Test encoding of target=0, LUN=8191.
  virtio::ScsiDevice::FillLUNStructure(&req, /*target=*/0, /*lun=*/8191);
  EXPECT_EQ(req.lun[0], 1);
  EXPECT_EQ(req.lun[1], 0);
  EXPECT_EQ(req.lun[2], 0x40 | 0x1F);
  EXPECT_EQ(req.lun[3], 0xFF);

  memset(&req, 0, sizeof(req));
  // Test encoding of target=0, LUN=16383 (highest allowed LUN).
  virtio::ScsiDevice::FillLUNStructure(&req, /*target=*/0, /*lun=*/16383);
  EXPECT_EQ(req.lun[0], 1);
  EXPECT_EQ(req.lun[1], 0);
  EXPECT_EQ(req.lun[2], 0x40 | 0x3F);
  EXPECT_EQ(req.lun[3], 0xFF);
}

}  // anonymous namespace
