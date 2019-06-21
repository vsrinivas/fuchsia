// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <map>
#include <ddk/binding.h>
#include <lib/fake_ddk/fake_ddk.h>
#include <lib/scsi/scsilib.h>
#include <lib/scsi/scsilib_controller.h>
#include <lib/fit/function.h>
#include <zxtest/zxtest.h>

namespace {


// Binder captures a scsi::Disk when device_add() is invoked inside the DDK.
class Binder : public fake_ddk::Bind {
  public:
    zx_status_t DeviceAdd(zx_driver_t* drv, zx_device_t* parent, device_add_args_t* args,
                          zx_device_t** out) {
        device_ = reinterpret_cast<scsi::Disk*>(args->ctx);
        return Base::DeviceAdd(drv, parent, args, out);
    }

    scsi::Disk* device() const { return device_; }

  private:
    using Base = fake_ddk::Bind;
    scsi::Disk* device_;
};

// ScsiController for test; allows us to set expectations and fakes command responses.
class ScsiControllerForTest : public scsi::Controller {
  public:
    using IOCallbackType = fit::function<zx_status_t(uint8_t, uint16_t, struct iovec, struct iovec,
                                                     struct iovec)>;

    ~ScsiControllerForTest() {
        ASSERT_EQ(times_, 0);
    }

    zx_status_t ExecuteCommandSync(uint8_t target, uint16_t lun, struct iovec cdb,
                                   struct iovec data_out, struct iovec data_in) override {
        EXPECT_TRUE(do_io_);
        EXPECT_GT(times_, 0);

        if (!do_io_ || times_ == 0) {
            return ZX_ERR_INTERNAL;
        }

        auto status = do_io_(target, lun, cdb, data_out, data_in);
        if (--times_ == 0) {
            decltype(do_io_) empty;
            do_io_.swap(empty);
        }
        return status;
    }

    void ExpectCall(IOCallbackType do_io, int times) {
        do_io_.swap(do_io);
        times_ = times;
    }

  private:
    IOCallbackType do_io_;
    int times_ = 0;
};

class ScsilibDiskTest : public zxtest::Test {
  public:
    static constexpr uint32_t kBlockSize = 512;
    static constexpr uint64_t kFakeBlocks = 128000ul;

    using DiskBlock = unsigned char[kBlockSize];

    void SetupDefaultCreateExpectations() {
        controller_.ExpectCall([this](uint8_t target, uint16_t lun, struct iovec cdb,
                                      struct iovec data_out, struct iovec data_in) -> auto {
            switch (default_seq_) {
            case 0: {
                scsi::InquiryCDB decoded_cdb = {};
                memcpy(&decoded_cdb, cdb.iov_base, cdb.iov_len);
                EXPECT_EQ(decoded_cdb.opcode, scsi::Opcode::INQUIRY);
                break;
            }
            case 1: {
                scsi::ReadCapacity16CDB decoded_cdb = {};
                memcpy(&decoded_cdb, cdb.iov_base, cdb.iov_len);
                scsi::ReadCapacity16ParameterData response = {};
                response.returned_logical_block_address = htobe64(kFakeBlocks - 1);
                response.block_length_in_bytes = htobe32(kBlockSize);
                memcpy(data_in.iov_base, reinterpret_cast<char*>(&response), sizeof(response));
                break;
            }
            }
            default_seq_++;

            return ZX_OK;
        }, /*times=*/2);
    }

    ScsiControllerForTest controller_;
    int default_seq_ = 0;
};

// Test that we can create a disk when the underlying controller successfully executes CDBs.
TEST_F(ScsilibDiskTest, TestCreateDestroy) {
    static constexpr uint8_t kTarget = 5;
    static constexpr uint16_t kLun = 1;
    static constexpr int kTransferSize = 32 * 1024;

    int seq = 0;
    controller_.ExpectCall([&seq](uint8_t target, uint16_t lun, struct iovec cdb,
                                  struct iovec data_out, struct iovec data_in) -> auto {
        EXPECT_EQ(target, kTarget);
        EXPECT_EQ(lun, kLun);

        if (seq == 0) {
            // INQUIRY is expected first.
            EXPECT_EQ(cdb.iov_len, 6);
            scsi::InquiryCDB decoded_cdb = {};
            memcpy(reinterpret_cast<scsi::InquiryCDB*>(&decoded_cdb), cdb.iov_base, cdb.iov_len);
            EXPECT_EQ(decoded_cdb.opcode, scsi::Opcode::INQUIRY);
        } else if (seq == 1) {
            // Then READ CAPACITY (16).
            EXPECT_EQ(cdb.iov_len, 16);
            scsi::ReadCapacity16CDB decoded_cdb = {};
            memcpy(reinterpret_cast<scsi::ReadCapacity16CDB*>(&decoded_cdb), cdb.iov_base,
                   cdb.iov_len);
            EXPECT_EQ(decoded_cdb.opcode, scsi::Opcode::READ_CAPACITY_16);
            EXPECT_EQ(decoded_cdb.service_action, 0x10);

            scsi::ReadCapacity16ParameterData response = {};
            response.returned_logical_block_address = htobe64(kFakeBlocks - 1);
            response.block_length_in_bytes = htobe32(kBlockSize);
            memcpy(data_in.iov_base, reinterpret_cast<char*>(&response), sizeof(response));
        }
        seq++;

        return ZX_OK;
    }, /*times=*/2);

    Binder bind;
    EXPECT_EQ(scsi::Disk::Create(&controller_, fake_ddk::kFakeParent, kTarget, kLun, kTransferSize),
              ZX_OK);
    EXPECT_EQ(bind.device()->DdkGetSize(), kFakeBlocks * kBlockSize);

    bind.device()->DdkRemove();
    EXPECT_TRUE(bind.Ok());
}

// Test creating a disk and executing read commands.
TEST_F(ScsilibDiskTest, TestCreateReadDestroy) {
    static constexpr uint8_t kTarget = 5;
    static constexpr uint16_t kLun = 1;
    static constexpr int kTransferSize = 32 * 1024;

    SetupDefaultCreateExpectations();

    Binder bind;
    EXPECT_EQ(scsi::Disk::Create(&controller_, fake_ddk::kFakeParent, kTarget, kLun, kTransferSize),
              ZX_OK);

    // To test SCSI Read functionality, create a fake "disk" backing store in memory and service
    // reads from it. Fill block 1 with a test pattern of 0x01.
    std::map<uint64_t, DiskBlock> blocks;
    DiskBlock& test_block_1 = blocks[1];
    memset(test_block_1, 0x01, sizeof(DiskBlock));

    controller_.ExpectCall([&blocks](uint8_t target, uint16_t lun, struct iovec cdb,
                                     struct iovec data_out, struct iovec data_in) -> auto {
        EXPECT_EQ(cdb.iov_len, 16);
        scsi::Read16CDB decoded_cdb = {};
        memcpy(&decoded_cdb, cdb.iov_base, cdb.iov_len);
        EXPECT_EQ(decoded_cdb.opcode, scsi::Opcode::READ_16);

        // Support reading one block.
        EXPECT_EQ(be32toh(decoded_cdb.transfer_length), 1);
        uint64_t block_to_read = be64toh(decoded_cdb.logical_block_address);
        const DiskBlock& data_to_return = blocks.at(block_to_read);
        memcpy(data_in.iov_base, data_to_return, sizeof(DiskBlock));

        return ZX_OK;
    }, /*times=*/1);

    // Issue a read to block 1 that should work.
    block_op_t read = {};
    block_impl_queue_callback done = [](void*, zx_status_t, block_op_t*) {};
    read.command = BLOCK_OP_READ;
    read.rw.length = 1;  // Read one block
    read.rw.offset_dev = 1;  // Read logical block 1
    read.rw.offset_vmo = 0;
    EXPECT_EQ(zx_vmo_create(PAGE_SIZE, 0, &read.rw.vmo), ZX_OK);
    bind.device()->BlockImplQueue(&read, done, nullptr);  // NOTE: Assumes synchronous controller

    // Make sure the contents of the VMO we read into match the expected test pattern
    DiskBlock check_buffer = {};
    EXPECT_EQ(zx_vmo_read(read.rw.vmo, check_buffer, 0, sizeof(DiskBlock)), ZX_OK);
    for (uint i = 0; i < sizeof(DiskBlock); i++) {
        EXPECT_EQ(check_buffer[i], 0x01);
    }

    bind.device()->DdkRemove();
    EXPECT_TRUE(bind.Ok());
}

} // namespace
