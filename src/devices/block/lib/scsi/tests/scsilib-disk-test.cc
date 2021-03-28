// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <endian.h>
#include <lib/fake_ddk/fake_ddk.h>
#include <lib/fit/function.h>
#include <lib/scsi/scsilib.h>
#include <lib/scsi/scsilib_controller.h>
#include <sys/types.h>
#include <zircon/listnode.h>

#include <map>

#include <fbl/auto_lock.h>
#include <fbl/condition_variable.h>
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
  using IOCallbackType =
      fit::function<zx_status_t(uint8_t, uint16_t, struct iovec, struct iovec, struct iovec)>;

  ~ScsiControllerForTest() { ASSERT_EQ(times_, 0); }

  // Init the state required for testing async IOs.
  zx_status_t AsyncIoInit() {
    {
      fbl::AutoLock lock(&lock_);
      list_initialize(&queued_ios_);
      worker_thread_exit_ = false;
    }
    auto cb = [](void* arg) -> int {
      return static_cast<ScsiControllerForTest*>(arg)->WorkerThread();
    };
    if (thrd_create_with_name(&worker_thread_, cb, this, "scsi-test-controller") != thrd_success) {
      printf("%s: Failed to create worker thread\n", __FILE__);
      return ZX_ERR_INTERNAL;
    }
    return ZX_OK;
  }

  // De-Init the state required for testing async IOs.
  void AsyncIoRelease() {
    {
      fbl::AutoLock lock(&lock_);
      worker_thread_exit_ = true;
      cv_.Signal();
    }
    thrd_join(worker_thread_, nullptr);
    list_node_t* node;
    list_node_t* temp_node;
    fbl::AutoLock lock(&lock_);
    list_for_every_safe(&queued_ios_, node, temp_node) {
      auto* io = containerof(node, struct queued_io, node);
      list_delete(node);
      free(io);
    }
  }

  zx_status_t ExecuteCommandAsync(uint8_t target, uint16_t lun, struct iovec cdb,
                                  struct iovec data_out, struct iovec data_in,
                                  void (*cb)(void*, zx_status_t), void* cookie) override {
    // In the caller, enqueue the request for the worker thread,
    // poke the worker thread and return. The worker thread, on
    // waking up, will do the actual IO and call the callback.
    auto* io = reinterpret_cast<struct queued_io*>(new queued_io);
    io->target = target;
    io->lun = lun;
    // The cbd is allocated on the stack in the scsilib's BlockImplQueue.
    // So make a copy of that locally, and point to that instead
    memcpy(reinterpret_cast<void*>(&io->cdbptr), cdb.iov_base, cdb.iov_len);
    io->cdb.iov_base = &io->cdbptr;
    io->cdb.iov_len = cdb.iov_len;
    io->data_out = data_out;
    io->data_in = data_in;
    io->cb = cb;
    io->cookie = cookie;
    fbl::AutoLock lock(&lock_);
    list_add_tail(&queued_ios_, &io->node);
    cv_.Signal();
    return ZX_OK;
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

  int WorkerThread() {
    fbl::AutoLock lock(&lock_);
    while (true) {
      if (worker_thread_exit_ == true)
        return ZX_OK;
      // While non-empty, remove requests and execute them
      list_node_t* node;
      list_node_t* temp_node;
      list_for_every_safe(&queued_ios_, node, temp_node) {
        auto* io = containerof(node, struct queued_io, node);
        list_delete(node);
        zx_status_t status;
        status = ExecuteCommandSync(io->target, io->lun, io->cdb, io->data_out, io->data_in);
        io->cb(io->cookie, status);
        delete io;
      }
      cv_.Wait(&lock_);
    }
    return ZX_OK;
  }

  struct queued_io {
    list_node_t node;
    uint8_t target;
    uint16_t lun;
    // Deep copy of the CDB.
    union {
      scsi::Read16CDB readcdb;
      scsi::Write16CDB writecdb;
    } cdbptr;
    struct iovec cdb;
    struct iovec data_out;
    struct iovec data_in;
    void (*cb)(void*, zx_status_t);
    void* cookie;
  };

  // These are the state for testing Async IOs.
  // The test enqueues Async IOs and pokes the worker thread, which
  // does the IO, and calls back.
  fbl::Mutex lock_;
  fbl::ConditionVariable cv_;
  thrd_t worker_thread_;
  bool worker_thread_exit_ __TA_GUARDED(lock_);
  list_node_t queued_ios_ __TA_GUARDED(lock_);
};

class ScsilibDiskTest : public zxtest::Test {
 public:
  static constexpr uint32_t kBlockSize = 512;
  static constexpr uint64_t kFakeBlocks = 128000ul;

  using DiskBlock = unsigned char[kBlockSize];

  void SetupDefaultCreateExpectations() {
    controller_.ExpectCall(
        [this](uint8_t target, uint16_t lun, struct iovec cdb, struct iovec data_out,
               struct iovec data_in) -> auto {
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
        },
        /*times=*/2);
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
  controller_.ExpectCall(
      [&seq](uint8_t target, uint16_t lun, struct iovec cdb, struct iovec data_out,
             struct iovec data_in) -> auto {
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
      },
      /*times=*/2);

  Binder bind;
  EXPECT_EQ(scsi::Disk::Create(&controller_, fake_ddk::kFakeParent, kTarget, kLun, kTransferSize),
            ZX_OK);
  EXPECT_EQ(bind.device()->DdkGetSize(), kFakeBlocks * kBlockSize);

  bind.device()->DdkAsyncRemove();
  EXPECT_OK(bind.WaitUntilRemove());
  bind.device()->DdkRelease();
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

  controller_.ExpectCall(
      [&blocks](uint8_t target, uint16_t lun, struct iovec cdb, struct iovec data_out,
                struct iovec data_in) -> auto {
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
      },
      /*times=*/1);

  // Issue a read to block 1 that should work.
  struct IoWait {
    fbl::Mutex lock_;
    fbl::ConditionVariable cv_;
  };
  IoWait iowait_;
  block_op_t read = {};
  block_impl_queue_callback done = [](void* ctx, zx_status_t status, block_op_t* op) {
    IoWait* iowait_ = reinterpret_cast<struct IoWait*>(ctx);

    fbl::AutoLock lock(&iowait_->lock_);
    iowait_->cv_.Signal();
  };
  read.command = BLOCK_OP_READ;
  read.rw.length = 1;      // Read one block
  read.rw.offset_dev = 1;  // Read logical block 1
  read.rw.offset_vmo = 0;
  EXPECT_OK(zx_vmo_create(PAGE_SIZE, 0, &read.rw.vmo));
  controller_.AsyncIoInit();
  {
    fbl::AutoLock lock(&iowait_.lock_);
    bind.device()->BlockImplQueue(&read, done, &iowait_);  // NOTE: Assumes asynchronous controller
    iowait_.cv_.Wait(&iowait_.lock_);
  }
  // Make sure the contents of the VMO we read into match the expected test pattern
  DiskBlock check_buffer = {};
  EXPECT_OK(zx_vmo_read(read.rw.vmo, check_buffer, 0, sizeof(DiskBlock)));
  for (uint i = 0; i < sizeof(DiskBlock); i++) {
    EXPECT_EQ(check_buffer[i], 0x01);
  }
  controller_.AsyncIoRelease();
  bind.device()->DdkAsyncRemove();
  EXPECT_OK(bind.WaitUntilRemove());
  bind.device()->DdkRelease();
  EXPECT_TRUE(bind.Ok());
}

}  // namespace
