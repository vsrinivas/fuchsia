// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/protocol/block.h>
#include <ddktl/device.h>
#include <ddktl/protocol/block.h>
#include <stdint.h>

namespace scsi {

enum class Opcode : uint8_t {
    TEST_UNIT_READY = 0x00,
    INQUIRY = 0x12,
    MODE_SENSE_6 = 0x1A,
    READ_16 = 0x88,
    WRITE_16 = 0x8A,
};

// SCSI command structures (CDBs)

struct TestUnitReadyCDB {
    Opcode opcode;
    uint8_t reserved[4];
    uint8_t control;
} __PACKED;

static_assert(sizeof(TestUnitReadyCDB) == 6, "TestUnitReady CDB must be 6 bytes");

class Disk;
using DeviceType = ddk::Device<Disk, ddk::GetSizable, ddk::Unbindable>;

// |Disk| represents a single SCSI direct access block device.
// |Disk| bridges between the Zircon block protocol and SCSI commands/responses.
class Disk : public DeviceType, public ddk::BlockImplProtocol<Disk, ddk::base_protocol> {
  public:
    // Public so that we can use make_unique.
    // Clients should use Disk::Create().
    Disk(zx_device_t* parent, uint8_t target, uint16_t lun);

    // Create a Disk at a specific target/lun.
    static zx_status_t Create(zx_device_t* parent, uint8_t target, uint16_t lun);

    const char* tag() const { return tag_; }

    // DeviceType functions.
    void DdkUnbind() { DdkRemove(); }
    void DdkRelease() { delete this; }

    // ddk::GetSizable functions.
    zx_off_t DdkGetSize() { return 0; }

    // ddk::BlockImplProtocol functions.
    // TODO(ZX-2314): Implement these two functions.
    void BlockImplQuery(block_info_t* info_out, size_t* block_op_size_out) {}
    void BlockImplQueue(block_op_t* operation, block_impl_queue_callback completion_cb,
                        void* cookie) {
        completion_cb(cookie, ZX_ERR_NOT_SUPPORTED, operation);
    }

    Disk(const Disk&) = delete;
    Disk& operator=(const Disk&) = delete;

  private:
    zx_status_t Bind();

    char tag_[24];
    const uint8_t target_;
    const uint16_t lun_;
};

} // namespace scsi
