// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <fbl/string_buffer.h>
#include <fbl/string_printf.h>
#include <fbl/unique_fd.h>
#include <fs-management/fvm.h>
#include <fs-management/ramdisk.h>
#include <fs-test-utils/fixture.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/memfs/memfs.h>
#include <sync/completion.h>
#include <zircon/assert.h>
#include <zircon/device/device.h>
#include <zircon/device/vfs.h>
#include <zircon/errors.h>

namespace fs_test_utils {

namespace {

// Mount point for local MemFs to be mounted.
constexpr char kMemFsPath[] = "/memfs";

// Name for MemFs serving thread.
constexpr char kMemFsThreadName[] = "TestServingMemFsName";

// Path where to mount the filesystem.
constexpr char kFsPath[] = "%s/fs-root";

// Partition name where the filesystem will be mounted when using fvm.
constexpr char kFsPartitionName[] = "fs-test-partition";

// FVM Driver lib
constexpr char kFvmDriverLibPath[] = "/boot/driver/fvm.so";

constexpr uint8_t kTestUniqueGUID[] = {
    0xFF, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
    0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f};

constexpr uint8_t kTestPartGUID[] = {
    0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
    0xFF, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07};

zx_status_t ToStatus(ssize_t result) {
    return result < 0 ? static_cast<zx_status_t>(result) : ZX_OK;
}

zx_status_t MountMemFs(async::Loop* loop) {
    zx_status_t result = ZX_OK;
    result = loop->StartThread(kMemFsThreadName);
    if (result != ZX_OK) {
        LOG_ERROR(result, "Failed to start serving thread for MemFs.\n");
        return result;
    }

    result = memfs_install_at(loop->dispatcher(), kMemFsPath);

    return result;
}

zx_status_t MakeRamdisk(const FixtureOptions& options, fbl::String* block_device_path) {
    ZX_DEBUG_ASSERT(options.use_ramdisk);
    if (options.use_ramdisk) {
        char buffer[kPathSize];
        zx_status_t result = create_ramdisk(options.ramdisk_block_size,
                                            options.ramdisk_block_count, buffer);
        if (result != ZX_OK) {
            LOG_ERROR(result,
                      "Failed to create ramdisk(block_size=%lu, ramdisk_block_count=%lu)\n",
                      options.ramdisk_block_size, options.ramdisk_block_count);
            return result;
        }
        *block_device_path = buffer;
    }

    return ZX_OK;
}

zx_status_t RemoveRamdisk(const FixtureOptions& options, const fbl::String& block_device_path) {
    ZX_DEBUG_ASSERT(options.use_ramdisk);
    if (options.use_ramdisk && !block_device_path.empty()) {
        zx_status_t result = destroy_ramdisk(block_device_path.c_str());
        if (result != ZX_OK) {
            LOG_ERROR(result, "Failed to destroy ramdisk.\nblock_device_path:%s\n",
                      block_device_path.c_str());
        }
    }
    return ZX_OK;
}

zx_status_t FormatDevice(const FixtureOptions& options, const fbl::String& block_device_path) {
    // Format device.
    mkfs_options_t mkfs_options = default_mkfs_options;
    zx_status_t result = mkfs(block_device_path.c_str(), options.fs_type,
                              launch_stdio_sync, &mkfs_options);
    if (result != ZX_OK) {
        LOG_ERROR(result, "Failed to format block device.\nblock_device_path:%s\n",
                  block_device_path.c_str());
        return result;
    }

    // Verify format.
    fsck_options_t fsck_options = default_fsck_options;
    result = fsck(block_device_path.c_str(), options.fs_type, &fsck_options, launch_stdio_sync);
    if (result != ZX_OK) {
        LOG_ERROR(result, "Block device format has errors.\nblock_device_path:%s\n",
                  block_device_path.c_str());
        return result;
    }

    return ZX_OK;
}

zx_status_t MountFs(const FixtureOptions& options, const fbl::String& block_device_path,
                    const fbl::String& mount_path) {
    zx_status_t result = FormatDevice(options, block_device_path);
    if (result != ZX_OK) {
        return result;
    }

    fbl::unique_fd fd(open(block_device_path.c_str(), O_RDWR));
    if (!fd) {
        LOG_ERROR(ZX_ERR_IO, "%s.\nblock_device_path:%s\n",
                  strerror(errno), block_device_path.c_str());
        return ZX_ERR_IO;
    }

    mount_options_t mount_options = default_mount_options;
    mount_options.create_mountpoint = true;
    mount_options.wait_until_ready = true;

    result = mount(fd.release(), mount_path.c_str(), options.fs_type,
                   &mount_options, launch_stdio_async);
    if (result != ZX_OK) {
        LOG_ERROR(result, "Failed to mount device at %s.\nblock_device_path:%s\n",
                  mount_path.c_str(), block_device_path.c_str());
        return result;
    }

    return ZX_OK;
}

zx_status_t UmountFs(const FixtureOptions& options, const fbl::String& block_device_path,
                     const fbl::String& mount_path) {
    if (!mount_path.empty()) {
        zx_status_t result = umount(mount_path.c_str());
        if (result != ZX_OK) {
            LOG_ERROR(result,
                      "Failed to umount device from MemFs.\nblock_device_path:%s\nmount_path:%s\n",
                      block_device_path.c_str(), mount_path.c_str());
            return result;
        }
    }
    return ZX_OK;
}

zx_status_t MakeFvm(const fbl::String& block_device_path, uint64_t fvm_slice_size,
                    fbl::String* partition_path, bool* fvm_mounted) {
    zx_status_t result = ZX_OK;
    fbl::unique_fd fd(open(block_device_path.c_str(), O_RDWR));
    if (!fd) {
        LOG_ERROR(ZX_ERR_IO, "%s.\nblock_device_path: %s\n",
                  strerror(errno), block_device_path.c_str());
        return ZX_ERR_IO;
    }
    result = fvm_init(fd.get(), fvm_slice_size);
    if (result != ZX_OK) {
        LOG_ERROR(result, "Failed to format device with FVM.\nblock_device_path: %s\n",
                  block_device_path.c_str());
        return result;
    }
    *fvm_mounted = true;
    fbl::String fvm_device_path = fbl::StringPrintf("%s/fvm", block_device_path.c_str());
    // Bind FVM Driver.
    result = ToStatus(ioctl_device_bind(fd.get(), kFvmDriverLibPath, strlen(kFvmDriverLibPath)));
    if (result != ZX_OK) {
        LOG_ERROR(result, "Failed to bind fvm driver to block device.\nblock_device:%s\n",
                  block_device_path.c_str());
        return result;
    }

    result = wait_for_device(fvm_device_path.c_str(), zx::sec(3).get());
    if (result != ZX_OK) {
        LOG_ERROR(result, "FVM driver failed to start.\nfvm_device_path:%s\n",
                  fvm_device_path.c_str());
        return result;
    }

    fbl::unique_fd fvm_fd(open(fvm_device_path.c_str(), O_RDWR));
    if (!fvm_fd) {
        LOG_ERROR(ZX_ERR_IO, "%s.\nfvm_device_path:%s\n",
                  strerror(errno), fvm_device_path.c_str());
        return ZX_ERR_IO;
    }

    alloc_req_t request;
    memset(&request, 0, sizeof(request));
    request.slice_count = 1;
    strcpy(request.name, kFsPartitionName);
    memcpy(request.type, kTestPartGUID, sizeof(request.type));
    memcpy(request.guid, kTestUniqueGUID, sizeof(request.guid));

    fbl::unique_fd partition_fd(fvm_allocate_partition(fvm_fd.get(), &request));
    if (!partition_fd) {
        LOG_ERROR(ZX_ERR_IO, "Failed to allocate FVM partition\n");
        return ZX_ERR_IO;
    }

    char buffer[kPathSize];
    partition_fd.reset(open_partition(kTestUniqueGUID, kTestPartGUID, 0, buffer));
    *partition_path = buffer;
    if (!partition_fd) {
        LOG_ERROR(ZX_ERR_IO, "Could not locate FVM partition. %s\n", strerror(errno));
        return ZX_ERR_IO;
    }

    return ZX_OK;
}

} // namespace

bool FixtureOptions::IsValid(fbl::String* err_description) const {
    ZX_DEBUG_ASSERT(err_description != nullptr);
    fbl::StringBuffer<400> buffer;
    err_description->clear();

    if (use_ramdisk) {
        if (!block_device_path.empty()) {
            buffer.Append("use_ramdisk and block_device_path are mutually exclusive.\n");
        }
        size_t max_size = zx_system_get_physmem();
        size_t requested_size = ramdisk_block_count * ramdisk_block_size;
        if (max_size < requested_size) {
            buffer.AppendPrintf(
                "ramdisk size(%lu) cannot exceed available memory(%lu).\n",
                requested_size, requested_size);
        }
        if (ramdisk_block_count == 0) {
            buffer.Append("ramdisk_block_count must be greater than 0.\n");
        }
        if (ramdisk_block_size == 0) {
            buffer.Append("ramdisk_block_size must be greater than 0.\n");
        }
    } else if (block_device_path.empty()) {
        buffer.Append("block_device_path or use_ramdisk must be set\n.");
    }

    if (use_fvm) {
        if (fvm_slice_size == 0 || fvm_slice_size % kFvmBlockSize != 0) {
            buffer.AppendPrintf("fvm_slize_size must be a multiple of %lu.\n", kFvmBlockSize);
        }
    }

    *err_description = buffer.ToString();

    return err_description->empty();
}

Fixture::Fixture(const FixtureOptions& options)
    : options_(options) {}

Fixture::~Fixture() {
    // Sanity check, teardown any resources if these two were not called yet.
    TearDown();
    TearDownTestCase();
};

zx_status_t Fixture::SetUpTestCase() {
    if (options_.use_ramdisk) {
        zx_status_t result = MakeRamdisk(options_, &block_device_path_);
        if (result != ZX_OK) {
            return result;
        }
        ramdisk_state_ = ResourceState::kAllocated;
    }

    if (!options_.block_device_path.empty()) {
        block_device_path_ = options_.block_device_path;
    }

    return ZX_OK;
}

zx_status_t Fixture::SetUp() {
    fvm_state_ = ResourceState::kUnallocated;
    fs_state_ = ResourceState::kUnallocated;
    if (options_.use_fvm) {
        bool allocated = false;
        zx_status_t result = MakeFvm(block_device_path_, options_.fvm_slice_size,
                                     &partition_path_, &allocated);
        if (allocated) {
            fvm_state_ = ResourceState::kAllocated;
        }

        if (result != ZX_OK) {
            return result;
        }
    }

    fs_path_ = fbl::StringPrintf(kFsPath, kMemFsPath);
    zx_status_t result = MountFs(options_, GetFsBlockDevice(), fs_path_);
    if (result != ZX_OK) {
        return result;
    }
    fs_state_ = ResourceState::kAllocated;
    return ZX_OK;
}

zx_status_t Fixture::TearDown() {
    zx_status_t result;
    // Umount Fs from MemFs.
    if (fs_state_ == ResourceState::kAllocated) {
        result = UmountFs(options_, block_device_path_, fs_path_);
        if (result != ZX_OK) {
            return result;
        }
    }

    // If real device not in FVM, clean it.
    if (!block_device_path_.empty() && !options_.use_fvm &&
        fs_state_ == ResourceState::kAllocated) {
        result = FormatDevice(options_, block_device_path_);
        if (result != ZX_OK) {
            return result;
        }
        fs_state_ = ResourceState::kFreed;
    }

    // If using FVM on top of device, just destroy the fvm, this only applies if
    // the fvm was created within this process.
    if (options_.use_fvm && fvm_state_ == ResourceState::kAllocated) {
        result = fvm_destroy(block_device_path_.c_str());
        if (result != ZX_OK) {
            LOG_ERROR(result, "Failed to destroy fvm in block_device.\nblock_device: %s\n",
                      block_device_path_.cend());
            return result;
        }
        fs_state_ = ResourceState::kFreed;
        fvm_state_ = ResourceState::kFreed;
    }
    return ZX_OK;
}

zx_status_t Fixture::TearDownTestCase() {
    if (ramdisk_state_ == ResourceState::kAllocated) {
        zx_status_t ramdisk_result = RemoveRamdisk(options_, block_device_path_);
        if (ramdisk_result != ZX_OK) {
            return ramdisk_result;
        }
    }
    ramdisk_state_ = ResourceState::kFreed;

    return ZX_OK;
}

int RunWithMemFs(const fbl::Function<int()>& main_fn) {
    async::Loop loop;
    if (MountMemFs(&loop) != ZX_OK) {
        return -1;
    }
    int result = main_fn();
    return result;
}

} // namespace fs_test_utils
