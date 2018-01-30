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
    kInit,
    kRunning,
    kComplete,
    kError,
};

class BlobfsTest {
public:
    BlobfsTest(FsTestType type) : type_(type) {}
    ~BlobfsTest();

    // Creates a ramdisk, formats it, and mounts it at a mount point.
    bool Init();

    // Unmounts and remounts the blobfs partition.
    bool Remount();

    // Unmounts a blobfs and removes the backing ramdisk device.
    // With |minimal| set, the umount and verification (e.g. fsck) methods will be excluded.
    bool Teardown(bool minimal = false);

    int GetFd() const {
        return open(ramdisk_path_, O_RDWR);
    }

    off_t GetDiskSize() const {
        return blk_size_ * blk_count_;
    }

    // Returns the full device path of blobfs.
    bool GetDevicePath(char* path, size_t len) const;

    // Sets readonly to |readonly|, defaulting to true.
    void SetReadOnly(bool read_only) {
        read_only_ = read_only;
    }

    // Sleeps or wakes the ramdisk underlying the blobfs partition, depending on its current state.
    bool ToggleSleep();

private:
    // Checks info of mounted blobfs.
    bool CheckInfo(const char* mount_path);

    // Mounts the blobfs partition with.
    bool MountInternal();

    FsTestType type_;
    FsTestState state_ = FsTestState::kInit;
    uint64_t blk_size_ = 512;
    uint64_t blk_count_ = 1 << 20;
    char ramdisk_path_[PATH_MAX];
    char fvm_path_[PATH_MAX];
    bool read_only_ = false;
    bool asleep_ = false;
};
}  // namespace