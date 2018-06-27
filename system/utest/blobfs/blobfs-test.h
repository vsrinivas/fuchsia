// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <limits.h>
#include <stdint.h>

namespace {

enum class FsTestType {
    // The partition may appear as any generic block device
    kNormal,

    // The partition should appear on top of a resizable
    // FVM device
    kFvm,
};

enum class FsTestState {
    kInit, // Just created, waiting to be initialized.
    kMinimal, // Initialized in a minimal state, i.e. ramdisk only.
    kRunning, // Initialized and ready to start testing.
    kComplete, // Indicates that the test has completed.
    kError, // Indicates that an error has occurred.
};

class BlobfsTest {
public:
    BlobfsTest(FsTestType type) : type_(type) {}
    ~BlobfsTest();

    // Creates a ramdisk, formats it, and mounts it at a mount point.
    // |state| indicates the intended state once initialization is complete.
    // This value must be either kMinimal or kRunning.
    // If |state| is kMinimal, the mkfs and mount methods will be skipped.
    bool Init(FsTestState state = FsTestState::kRunning);

    // Unmounts and remounts the blobfs partition.
    bool Remount();

    // Unmounts a blobfs, runs fsck, and removes the backing ramdisk device.
    // |state| indicates the expected state before teardown begins.
    // This value must be either kMinimal or kRunning.
    // If |state| is kMinimal, the umount and fsck methods will be skipped.
    bool Teardown(FsTestState state = FsTestState::kRunning);

    int GetFd() const {
        return open(ramdisk_path_, O_RDWR);
    }

    off_t GetDiskSize() const {
        return blk_size_ * blk_count_;
    }

    uint64_t GetBlockSize() const {
        return blk_size_;
    }

    // Returns the full device path of blobfs.
    bool GetDevicePath(char* path, size_t len) const;

    // Given a new disk size, updates the block count. Block size doesn't change.
    bool SetBlockCount(uint64_t block_count) {
        BEGIN_HELPER;
        ASSERT_EQ(state_, FsTestState::kInit);
        blk_count_ = block_count;
        END_HELPER;
    }

    // Sets readonly to |readonly|, defaulting to true.
    void SetReadOnly(bool read_only) {
        read_only_ = read_only;
    }

    // Determine if the mounted filesystem should have output to stdio.
    void SetStdio(bool stdio) {
        stdio_ = stdio;
    }

    // Reset to initial state, given that the test was successfully torn down.
    bool Reset() {
        BEGIN_HELPER;
        ASSERT_EQ(state_, FsTestState::kComplete);
        state_ = FsTestState::kInit;
        END_HELPER;
    }

    // Sleeps or wakes the ramdisk underlying the blobfs partition, depending on its current state.
    bool ToggleSleep();

private:
    // Checks info of mounted blobfs.
    bool CheckInfo(const char* mount_path);

    // Mounts the blobfs partition.
    bool Mount();

    FsTestType type_;
    FsTestState state_ = FsTestState::kInit;
    uint64_t blk_size_ = 512;
    uint64_t blk_count_ = 1 << 20;
    char ramdisk_path_[PATH_MAX];
    char fvm_path_[PATH_MAX];
    bool read_only_ = false;
    bool asleep_ = false;
    bool stdio_ = true;
};

}  // namespace
