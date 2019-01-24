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

struct InquiryCDB {
    Opcode opcode;
    // reserved_and_evpd(0) is 'Enable Vital Product Data'
    uint8_t reserved_and_evpd;
    uint8_t page_code;
    // allocation_length is in network-byte-order.
    uint16_t allocation_length;
    uint8_t control;
} __PACKED;

static_assert(sizeof(InquiryCDB) == 6, "Inquiry CDB must be 6 bytes");

// Standard INQUIRY Data Header.
struct InquiryData {
   // Peripheral Device Type Header and qualifier.
   uint8_t peripheral_device_type;
   // removable(7) is the 'Removable' bit.
   uint8_t removable;
   uint8_t version;
   // response_data_format_and_control(3 downto 0) is Response Data Format
   // response_data_format_and_control(4) is HiSup
   // response_data_format_and_control(5) is NormACA
   uint8_t response_data_format_and_control;
   uint8_t additional_length;
   // Various control bits, unused currently.
   uint8_t control[3];
   uint8_t t10_vendor_id[8];
   uint8_t product_id[16];
   uint8_t product_revision[4];
   uint8_t drive_serial_number[8];
} __PACKED;

static_assert(offsetof(InquiryData, t10_vendor_id) == 8, "T10 Vendor ID is at offset 8");
static_assert(offsetof(InquiryData, product_id) == 16, "Product ID is at offset 16");

struct ModeSense6CDB {
    Opcode opcode;
    // If disable_block_descriptors(4) is '1', device will not return Block Descriptors.
    uint8_t disable_block_descriptors;
    // page_code(7 downto 6) is 'page control'. Should be 00h for current devices.
    uint8_t page_code;
    uint8_t subpage_code;
    uint8_t allocation_length;
    uint8_t control;
} __PACKED;

static_assert(sizeof(ModeSense6CDB) == 6, "Mode Sense 6 CDB must be 6 bytes");

struct ModeSense6ParameterHeader {
    uint8_t mode_data_length;
    // 00h is 'Direct Access Block Device'
    uint8_t medium_type;
    // For Direct Access Block Devices:
    // device_specific_parameter(7) is write-protected bit
    // device_specific_parameter(4) is disable page out/force unit access available
    uint8_t device_specific_parameter;
    uint8_t block_descriptor_length;
} __PACKED;

static_assert(sizeof(ModeSense6ParameterHeader) == 4, "Mode sense 6 parameters must be 4 bytes");

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
