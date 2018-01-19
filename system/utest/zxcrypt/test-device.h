// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stddef.h>
#include <stdint.h>
#include <unistd.h>

#include <block-client/client.h>
#include <fbl/macros.h>
#include <fbl/unique_fd.h>
#include <fvm/fvm.h>
#include <zircon/status.h>
#include <zircon/types.h>
#include <zx/vmo.h>

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

    // Returns a reference to the root key generated for this device.
    const crypto::Bytes& key() const { return key_; }

    // Generates a key of an appropriate length for the given |version|.
    zx_status_t GenerateKey(Volume::Version version);

    // Allocates a new block device of at least |device_size| bytes grouped into blocks of
    // |block_size| bytes each.  If |fvm| is true, it will be formatted as an FVM partition with the
    // appropriates number of slices of |FVM_BLOCK_SIZE| each.  A file descriptor for the block
    // device is returned via |out_fd|.
    zx_status_t Create(size_t device_size, size_t block_size, bool fvm);

    // Binds the zxcrypt driver to the current block device.  It is an error to call this method
    // without calling |Create|.  If the driver was previously bound, this will trigger the
    // underlying device to unbind and rebind its children.
    zx_status_t BindZxcrypt();

    // Convenience method that generates a key and creates a device according to |version| and
    // |fvm|.  It sets up the device as a zxcrypt volume and binds to it.
    zx_status_t DefaultInit(Volume::Version version, bool fvm);

    // Flips a (pseudo)random bit in the byte at the given |offset| on the block device.  The call
    // to |srand| in main.c guarantees the same bit will be chosen for a given test iteration.
    zx_status_t Corrupt(zx_off_t offset);

    // Seeks to the given |offset| in the zxcrypt device, and writes|length| bytes from an internal
    // write buffer containing a pseudo-random byte sequence.
    zx_status_t WriteFd(zx_off_t offset, size_t length) {
        return Write(zxcrypt_, to_write_.get(), offset, length);
    }

    // Seeks to the given |offset| in the zxcrypt device, and reads |length| bytes into an internal
    // read buffer.
    zx_status_t ReadFd(zx_off_t offset, size_t length) {
        return Read(zxcrypt_, as_read_.get(), offset, length);
    }

    // Writes up to |length| bytes from an internal write buffer to the given |offset| in the
    // device.
    zx_status_t WriteVmo(zx_off_t offset, size_t length);

    // Reads up to |length| bytes from the given |offset| in the device into an internal read
    // buffer.
    zx_status_t ReadVmo(zx_off_t offset, size_t length);

    // Returns true if and only if |length| starting from the given |offset| bytes of the internal
    // read and write buffers match, i.e. if the data was written and read back correctly using
    // the methods below.
    bool CheckMatch(zx_off_t offset, size_t length) const;

private:
    DISALLOW_COPY_ASSIGN_AND_MOVE(TestDevice);

    // Allocates a new ramdisk of at least |device_size| bytes arranged into blocks of |block_size|
    // bytes, and opens it.
    zx_status_t CreateRamdisk(size_t device_size, size_t block_size);

    // Creates a ramdisk of with enough blocks of |block_size| bytes to hold both FVM metadata and
    // an FVM partition of at least |device_size| bytes.  It formats the ramdisk to be an FVM
    // device, and allocates a partition with a single slice of size FVM_BLOCK_SIZE.
    zx_status_t CreateFvmPart(size_t device_size, size_t block_size);

    // Seeks to the given |offset| in |fd|, and writes |length| bytes from |buf|.
    zx_status_t Write(const fbl::unique_fd& fd, const uint8_t* buf, zx_off_t offset, size_t length);

    // Seeks to the given |offset| in |fd|, and reads |length| bytes into |buf|.
    zx_status_t Read(const fbl::unique_fd& fd, uint8_t* buf, zx_off_t offset, size_t length);

    // Tears down the current ramdisk and all its children.
    void Reset();

    // Indicates if create_ramdisk was called successfully.
    bool has_ramdisk_;
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
    // The cached block count.
    size_t block_count_;
    // The cached block size.
    size_t block_size_;
    // The root key for this device.
    crypto::Bytes key_;
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
};

} // namespace testing
} // namespace zxcrypt
