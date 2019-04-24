// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/binding.h>
#include <lib/fake_ddk/fake_ddk.h>
#include <lib/scsi/scsilib.h>
#include <lib/scsi/scsilib_controller.h>
#include <lib/fit/function.h>
#include <zxtest/zxtest.h>

namespace {

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

// Test that we can create a disk when the underlying controller successfully executes CDBs.
TEST(ScsilibDiskTest, TestCreateDestroy) {
    static constexpr uint8_t kTarget = 5;
    static constexpr uint16_t kLun = 1;
    static constexpr int kTransferSize = 32 * 1024;
    static constexpr uint64_t kFakeBlocks = 128000ul;
    static constexpr uint32_t kBlockSize = 512;

    ScsiControllerForTest controller;
    int seq = 0;
    controller.ExpectCall([&seq](uint8_t target, uint16_t lun, struct iovec cdb,
                                 struct iovec data_out, struct iovec data_in) -> auto {
        EXPECT_EQ(target, kTarget);
        EXPECT_EQ(lun, kLun);

        if (seq == 0) {
            // INQUIRY is expected first.
            EXPECT_EQ(cdb.iov_len, 6);
            scsi::InquiryCDB decoded_cdb = {};
            memcpy(reinterpret_cast<scsi::InquiryCDB*>(&decoded_cdb), cdb.iov_base, cdb.iov_len);
            EXPECT_EQ(decoded_cdb.opcode, scsi::Opcode::INQUIRY);
        } else {
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
    EXPECT_EQ(scsi::Disk::Create(&controller, fake_ddk::kFakeParent, kTarget, kLun, kTransferSize),
              ZX_OK);
    EXPECT_EQ(bind.device()->DdkGetSize(), kFakeBlocks * kBlockSize);
    bind.device()->DdkRemove();
    EXPECT_TRUE(bind.Ok());
}

} // namespace
