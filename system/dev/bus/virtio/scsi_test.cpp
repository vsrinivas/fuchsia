// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fake_ddk/fake_ddk.h>
#include <unittest/unittest.h>

#include "backends/fake.h"
#include "scsi.h"

using Queue = virtio::ScsiDevice::Queue;

namespace {

// Fake virtio 'backend' for a virtio-scsi device.
class FakeBackendForScsi : public virtio::FakeBackend {
  public:
    FakeBackendForScsi() : virtio::FakeBackend(
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

bool InitTest() {
    BEGIN_TEST;
    fbl::unique_ptr<virtio::Backend> backend = fbl::make_unique<FakeBackendForScsi>();
    zx::bti bti(ZX_HANDLE_INVALID);

    virtio::ScsiDevice scsi(/*parent=*/nullptr, std::move(bti), std::move(backend));
    auto status = scsi.Init();
    EXPECT_NE(status, ZX_OK);
    END_TEST;
}

}  // anonymous namespace

BEGIN_TEST_CASE(ScsiDriverTests)
RUN_TEST_SMALL(InitTest)
END_TEST_CASE(ScsiDriverTests)
