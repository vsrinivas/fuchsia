// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stddef.h>
#include <stdint.h>
#include <threads.h>
#include <unistd.h>

#include <crypto/secret.h>
#include <block-client/client.h>
#include <fbl/macros.h>
#include <fbl/mutex.h>
#include <fbl/unique_fd.h>
#include <fvm/fvm.h>
#include <lib/zx/vmo.h>
#include <zircon/compiler.h>
#include <zircon/status.h>
#include <zircon/types.h>

#include "crypto/utils.h"

#define DEFINE_EACH_DEVICE(Test)                                                                   \
    bool Test##Raw(Volume::Version version) { return Test(version, false /* not FVM */); }         \
    DEFINE_EACH(Test##Raw);                                                                        \
    bool Test##Fvm(Volume::Version version) { return Test(version, true /* FVM */); }              \
    DEFINE_EACH(Test##Fvm);

#define RUN_EACH_DEVICE(Test)                                                                      \
    RUN_EACH(Test##Raw)                                                                            \
    RUN_EACH(Test##Fvm)

namespace zxcrypt {
namespace testing {

// Default disk geometry to use when testing device block related code.
const uint32_t kBlockCount = 64;
const uint32_t kBlockSize = 512;
const size_t kDeviceSize = kBlockCount * kBlockSize;
const uint32_t kSliceCount = kDeviceSize / FVM_BLOCK_SIZE;

// |zxcrypt::testing::Utils| is a collection of functions designed to make the zxcrypt
// unit test setup and tear down easier.
class TestDevice final {
public:
    explicit TestDevice();
    ~TestDevice();

    // ACCESSORS

    // Returns the size of the zxcrypt volume.
    size_t size() const { return block_count_ * block_size_; }

    // Returns a duplicated file descriptor representing the zxcrypt volume's underlying device;
    // that is, the ramdisk or FVM partition.
    fbl::unique_fd parent() const {
        return fbl::unique_fd(dup(fvm_part_ ? fvm_part_.get() : ramdisk_.get()));
    }

    // Returns a duplicated file descriptor representing t the zxcrypt volume.
    fbl::unique_fd zxcrypt() const { return fbl::unique_fd(dup(zxcrypt_.get())); }

    // Returns the block size of the zxcrypt device.
    size_t block_size() const { return block_size_; }

    // Returns the block size of the zxcrypt device.
    size_t block_count() const { return block_count_; }

    // Returns space reserved for metadata
    size_t reserved_blocks() const { return volume_->reserved_blocks(); }
    size_t reserved_slices() const { return volume_->reserved_slices(); }

    // Returns a reference to the root key generated for this device.
    const crypto::Secret& key() const { return key_; }

    // API WRAPPERS

    // These methods mirror the POSIX API, except that the file descriptors and buffers are
    // provided automatically. |off| and |len| are in bytes.
    ssize_t lseek(zx_off_t off) { return ::lseek(zxcrypt_.get(), off, SEEK_SET); }
    ssize_t read(zx_off_t off, size_t len) {
        return ::read(zxcrypt_.get(), as_read_.get() + off, len);
    }
    ssize_t write(zx_off_t off, size_t len) {
        return ::write(zxcrypt_.get(), to_write_.get() + off, len);
    }

    // These methods mirror the syscall API, except that the VMO and buffers are provided
    // automatically. |off| and |len| are in bytes.
    zx_status_t vmo_read(zx_off_t off, size_t len) {
        return vmo_.read(as_read_.get() + off, 0, len);
    }
    zx_status_t vmo_write(uint64_t off, uint64_t len) {
        return vmo_.write(to_write_.get() + off, 0, len);
    }

    // Sends a request over the block fifo to read or write the blocks given by |off| and |len|,
    // according to the given |opcode|.  The data sent or received can be accessed using |vmo_write|
    // or |vmo_read|, respectively.  |off| and |len| are in blocks.
    zx_status_t block_fifo_txn(uint16_t opcode, uint64_t off, uint64_t len) {
        req_.opcode = opcode;
        req_.length = static_cast<uint32_t>(len);
        req_.dev_offset = off;
        req_.vmo_offset = 0;
        return ::block_fifo_txn(client_, &req_, 1);
    }

    // Sends |num| requests over the block fifo to read or write blocks.
    zx_status_t block_fifo_txn(block_fifo_request_t* requests, size_t num) {
        for (size_t i = 0; i < num; ++i) {
            requests[i].group = req_.group;
            requests[i].vmoid = req_.vmoid;
        }
        return ::block_fifo_txn(client_, requests, num);
    }

    // TEST HELPERS

    // Allocates a new block device of at least |device_size| bytes grouped into blocks of
    // |block_size| bytes each.  If |fvm| is true, it will be formatted as an FVM partition with the
    // appropriates number of slices of |FVM_BLOCK_SIZE| each.  A file descriptor for the block
    // device is returned via |out_fd|.
    bool Create(size_t device_size, size_t block_size, bool fvm);

    // Test helper that generates a key and creates a device according to |version| and |fvm|.  It
    // sets up the device as a zxcrypt volume and binds to it.
    bool Bind(Volume::Version version, bool fvm);

    // Test helper that rebinds the ramdisk and its children.
    bool Rebind();

    // Tells the underlying ramdisk to sleep until |num| transactions have been received.  If
    // |deferred| is true, the transactions will be handled on waking; else they will be failed.
    bool SleepUntil(uint64_t num, bool deferred) __TA_EXCLUDES(lock_);

    // Blocks until the ramdisk is awake.
    bool WakeUp() __TA_EXCLUDES(lock_);

    // Test helpers that perform a |lseek| and a |read| or |write| together. |off| and |len| are in
    // bytes.  |ReadFd| additionally checks that the data read matches what was written.
    bool ReadFd(zx_off_t off, size_t len);
    bool WriteFd(zx_off_t off, size_t len);

    // Test helpers that perform a |lseek| and a |vmo_read| or |vmo_write| together.  |off| and
    // |len| are in blocks.  |ReadVmo| additionally checks that the data read matches what was
    // written.
    bool ReadVmo(zx_off_t off, size_t len);
    bool WriteVmo(zx_off_t off, size_t len);

    // Test helper that flips a (pseudo)random bit in the byte at the given |offset| on the block
    // device.  The call to |srand| in main.c guarantees the same bit will be chosen for a given
    // test iteration.
    bool Corrupt(zx_off_t offset);

private:
    DISALLOW_COPY_ASSIGN_AND_MOVE(TestDevice);

    // Allocates a new ramdisk of at least |device_size| bytes arranged into blocks of |block_size|
    // bytes, and opens it.
    bool CreateRamdisk(size_t device_size, size_t block_size);

    // Destroys the ramdisk, killing any active transactions
    void DestroyRamdisk();

    // Creates a ramdisk of with enough blocks of |block_size| bytes to hold both FVM metadata and
    // an FVM partition of at least |device_size| bytes.  It formats the ramdisk to be an FVM
    // device, and allocates a partition with a single slice of size FVM_BLOCK_SIZE.
    bool CreateFvmPart(size_t device_size, size_t block_size);

    // Connects the block client to the block server.
    bool Connect();

    // Disconnects the block client from the block server.
    void Disconnect();

    // Thread body to wake up the underlying ramdisk.
    static int WakeThread(void* arg);

    // The pathname of the ramdisk
    char ramdisk_path_[PATH_MAX];
    // The pathname of the FVM partition.
    char fvm_part_path_[PATH_MAX];
    // File descriptor for the underlying ramdisk.
    fbl::unique_fd ramdisk_;
    // File descriptor for the (optional) underlying FVM partition.
    fbl::unique_fd fvm_part_;
    // File descriptor for the zxcrypt volume.
    fbl::unique_fd zxcrypt_;
    // The zxcrypt volume
    fbl::unique_ptr<Volume> volume_;
    // The cached block count.
    size_t block_count_;
    // The cached block size.
    size_t block_size_;
    // The root key for this device.
    crypto::Secret key_;
    // Client for the block I/O protocol to the block server.
    fifo_client_t* client_;
    // Request structure used to send messages via the block I/O protocol.
    block_fifo_request_t req_;
    // VMO attached to the zxcrypt device for use with the block I/O protocol.
    zx::vmo vmo_;
    // An internal write buffer, initially filled with pseudo-random data
    fbl::unique_ptr<uint8_t[]> to_write_;
    // An internal write buffer,  initially filled with zeros.
    fbl::unique_ptr<uint8_t[]> as_read_;
    // Lock to coordinate waking thread
    fbl::Mutex lock_;
    // Thread used to manage sleeping/waking.
    thrd_t tid_;
    // It would be nice if thrd_t had a reserved invalid value...
    bool need_join_;
    // The number of transactions before waking.
    uint64_t wake_after_ __TA_GUARDED(lock_);
    // Timeout before waking regardless of transactions
    zx::time wake_deadline_ __TA_GUARDED(lock_);
};

} // namespace testing
} // namespace zxcrypt
