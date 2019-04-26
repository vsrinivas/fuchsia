// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <fbl/algorithm.h>
#include <fbl/auto_call.h>
#include <fbl/string_buffer.h>
#include <fbl/unique_fd.h>
#include <fs-management/mount.h>
#include <fuchsia/device/c/fidl.h>
#include <fuchsia/hardware/block/c/fidl.h>
#include <fuchsia/hardware/block/partition/c/fidl.h>
#include <gpt/gpt.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <lib/fdio/unsafe.h>
#include <lib/fdio/watcher.h>
#include <lib/fzl/fdio.h>
#include <lib/fzl/time.h>
#include <lib/zx/channel.h>
#include <lib/zx/process.h>
#include <lib/zx/time.h>
#include <loader-service/loader-service.h>
#include <minfs/minfs.h>
#include <ramdevice-client/ramdisk.h>
#include <zircon/device/block.h>
#include <zircon/processargs.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>
#include <zxcrypt/fdio-volume.h>

#include <utility>

#include "block-watcher.h"
#include "pkgfs-launcher.h"

namespace devmgr {
namespace {

zx_status_t LaunchBlobfs(int argc, const char** argv, zx_handle_t* hnd, uint32_t* ids,
                          size_t len) {
    return devmgr_launch(*zx::job::default_job(), "blobfs:/blob", argv, nullptr,
                         -1, hnd, ids, len, nullptr, FS_FOR_FSPROC);
}

zx_status_t LaunchMinfs(int argc, const char** argv, zx_handle_t* hnd, uint32_t* ids, size_t len) {
    return devmgr_launch(*zx::job::default_job(), "minfs:/data", argv, nullptr,
                         -1, hnd, ids, len, nullptr, FS_FOR_FSPROC);
}

zx_status_t LaunchFAT(int argc, const char** argv, zx_handle_t* hnd, uint32_t* ids, size_t len) {
    return devmgr_launch(*zx::job::default_job(), "fatfs:/volume", argv, nullptr,
                         -1, hnd, ids, len, nullptr, FS_FOR_FSPROC);
}

// Attempt to mount the device pointed to be the file descriptor at a known
// location.
//
// Returns ZX_ERR_ALREADY_BOUND if the device could be mounted, but something
// is already mounted at that location. Returns ZX_ERR_INVALID_ARGS if the
// GUID of the device does not match a known valid one. Returns
// ZX_ERR_NOT_SUPPORTED if the GUID is a system GUID. Returns ZX_OK if an
// attempt to mount is made, without checking mount success.
zx_status_t MountMinfs(BlockWatcher* watcher, fbl::unique_fd fd, mount_options_t* options) {
    fuchsia_hardware_block_partition_GUID type_guid;
    {
        fzl::UnownedFdioCaller disk_connection(fd.get());
        zx::unowned_channel channel(disk_connection.borrow_channel());
        zx_status_t io_status, status;
        io_status = fuchsia_hardware_block_partition_PartitionGetTypeGuid(channel->get(), &status,
                                                                          &type_guid);
        if (io_status != ZX_OK)
            return io_status;
        if (status != ZX_OK)
            return status;
    }

    if (gpt_is_sys_guid(type_guid.value, GPT_GUID_LEN)) {
        return ZX_ERR_NOT_SUPPORTED;
    } else if (gpt_is_data_guid(type_guid.value, GPT_GUID_LEN)) {
        return watcher->MountData(std::move(fd), options);
    } else if (gpt_is_install_guid(type_guid.value, GPT_GUID_LEN)) {
        return watcher->MountInstall(std::move(fd), options);
    }
    printf("fshost: Unrecognized partition GUID for minfs; not mounting\n");
    return ZX_ERR_INVALID_ARGS;
}

#define FVM_DRIVER_LIB "/boot/driver/fvm.so"
#define GPT_DRIVER_LIB "/boot/driver/gpt.so"
#define MBR_DRIVER_LIB "/boot/driver/mbr.so"
#define BOOTPART_DRIVER_LIB "/boot/driver/bootpart.so"
#define ZXCRYPT_DRIVER_LIB "/boot/driver/zxcrypt.so"
#define STRLEN(s) (sizeof(s) / sizeof((s)[0]))

// return value is ignored
int UnsealZxcrypt(void* arg) {
    std::unique_ptr<int> fd_ptr(static_cast<int*>(arg));
    fbl::unique_fd fd(*fd_ptr);

    zx_status_t rc;
    std::unique_ptr<zxcrypt::FdioVolume> zxcrypt_volume;
    if ((rc = zxcrypt::FdioVolume::Init(std::move(fd), &zxcrypt_volume)) != ZX_OK) {
        printf("fshost: couldn't open zxcrypt fdio volume");
        return ZX_OK;
    }

    zx::channel zxcrypt_volume_manager_chan;
    if ((rc = zxcrypt_volume->OpenManager(zx::sec(2), zxcrypt_volume_manager_chan.reset_and_get_address())) != ZX_OK) {
        printf("fshost: couldn't open zxcrypt manager device");
        return 0;
    }

    zxcrypt::FdioVolumeManager zxcrypt_volume_manager(std::move(zxcrypt_volume_manager_chan));
    uint8_t slot = 0;
    if ((rc = zxcrypt_volume_manager.UnsealWithDeviceKey(slot)) != ZX_OK) {
        printf("fshost: couldn't unseal zxcrypt manager device");
        return 0;
    }

    return 0;
}

zx_status_t FormatMinfs(const fbl::unique_fd& block_device,
                        const fuchsia_hardware_block_BlockInfo& info) {

    fprintf(stderr, "fshost: Formatting minfs.\n");
    uint64_t device_size = info.block_size * info.block_count;
    std::unique_ptr<minfs::Bcache> bc;
    zx_status_t status;
    if ((status = minfs::Bcache::Create(&bc, block_device.duplicate(),
                                        static_cast<uint32_t>(device_size))) != ZX_OK) {
        fprintf(stderr, "fshost: Could not initialize minfs bcache.\n");
        return status;
    }
    minfs::MountOptions options = {};
    if ((status = Mkfs(options, std::move(bc))) != ZX_OK) {
        fprintf(stderr, "fshost: Could not format minfs filesystem.\n");
        return status;
    }
    printf("fshost: Minfs filesystem re-formatted. Expect data loss.\n");
    return ZX_OK;
}

zx_status_t BlockDeviceAdded(int dirfd, int event, const char* name, void* cookie) {
    auto watcher = static_cast<BlockWatcher*>(cookie);

    if (event != WATCH_EVENT_ADD_FILE) {
        return ZX_OK;
    }

    char device_path[PATH_MAX];
    sprintf(device_path, "%s/%s", PATH_DEV_BLOCK, name);

    fbl::unique_fd fd(openat(dirfd, name, O_RDWR));
    if (!fd) {
        return ZX_OK;
    }

    disk_format_t df = detect_disk_format(fd.get());
    fuchsia_hardware_block_BlockInfo info;
    fuchsia_hardware_block_partition_GUID guid;
    {
        fzl::UnownedFdioCaller disk_connection(fd.get());
        zx::unowned_channel disk(disk_connection.borrow_channel());

        zx_status_t io_status, call_status;
        io_status = fuchsia_hardware_block_BlockGetInfo(disk->get(), &call_status, &info);
        if (io_status != ZX_OK || call_status != ZX_OK) {
            return ZX_OK;
        }

        if (info.flags & BLOCK_FLAG_BOOTPART) {
            fuchsia_device_ControllerBind(disk->get(), BOOTPART_DRIVER_LIB,
                                          STRLEN(BOOTPART_DRIVER_LIB), &call_status);
            return ZX_OK;
        }

        switch (df) {
        case DISK_FORMAT_GPT: {
            printf("fshost: %s: GPT?\n", device_path);
            // probe for partition table
            fuchsia_device_ControllerBind(disk->get(), GPT_DRIVER_LIB, STRLEN(GPT_DRIVER_LIB),
                                          &call_status);
            return ZX_OK;
        }
        case DISK_FORMAT_FVM: {
            printf("fshost: /dev/class/block/%s: FVM?\n", name);
            // probe for partition table
            fuchsia_device_ControllerBind(disk->get(), FVM_DRIVER_LIB, STRLEN(FVM_DRIVER_LIB),
                                          &call_status);
            return ZX_OK;
        }
        case DISK_FORMAT_MBR: {
            printf("fshost: %s: MBR?\n", device_path);
            // probe for partition table
            fuchsia_device_ControllerBind(disk->get(), MBR_DRIVER_LIB, STRLEN(MBR_DRIVER_LIB),
                                          &call_status);
            return ZX_OK;
        }
        case DISK_FORMAT_ZXCRYPT: {
            if (!watcher->Netbooting()) {
                printf("fshost: %s: zxcrypt?\n", device_path);
                // Bind and unseal the driver from a separate thread, since we
                // have to wait for a number of devices to do I/O and settle,
                // and we don't want to block block-watcher for any nontrivial
                // length of time.

                // We transfer fd to the spawned thread.  Since it's UB to cast
                // ints to pointers and back, we allocate the fd on the heap.
                int loose_fd = fd.release();
                int* raw_fd_ptr = new int(loose_fd);
                thrd_t th;
                int err = thrd_create_with_name(&th, &UnsealZxcrypt, raw_fd_ptr, "zxcrypt-unseal");
                if (err != thrd_success) {
                    printf("fshost: failed to spawn zxcrypt unseal thread");
                    close(loose_fd);
                    delete raw_fd_ptr;
                } else {
                    thrd_detach(th);
                }
            }
            return ZX_OK;
        }
        default:
            break;
        }

        io_status = fuchsia_hardware_block_partition_PartitionGetTypeGuid(disk->get(), &call_status,
                                                                          &guid);
        if (io_status != ZX_OK || call_status != ZX_OK) {
            return ZX_OK;
        }
    }

    // If we're in netbooting mode, then only bind drivers for partition
    // containers and the install partition, not regular filesystems.
    if (watcher->Netbooting()) {
        if (gpt_is_install_guid(guid.value, GPT_GUID_LEN)) {
            printf("fshost: mounting install partition\n");
            mount_options_t options = default_mount_options;
            MountMinfs(watcher, std::move(fd), &options);
            return ZX_OK;
        }

        return ZX_OK;
    }

    switch (df) {
    case DISK_FORMAT_BLOBFS: {
        const uint8_t expected_guid[GPT_GUID_LEN] = GUID_BLOB_VALUE;

        if (memcmp(guid.value, expected_guid, GPT_GUID_LEN)) {
            return ZX_OK;
        }
        fsck_options_t fsck_options = default_fsck_options;
        fsck_options.apply_journal = true;
        if (watcher->CheckFilesystem(device_path, DISK_FORMAT_BLOBFS, &fsck_options) != ZX_OK) {
            return ZX_OK;
        }

        mount_options_t options = default_mount_options;
        options.enable_journal = true;
        options.collect_metrics = true;
        zx_status_t status = watcher->MountBlob(std::move(fd), &options);
        if (status != ZX_OK) {
            printf("fshost: Failed to mount blobfs partition %s at %s: %s.\n", device_path,
                   PATH_BLOB, zx_status_get_string(status));
        } else {
            LaunchBlobInit(watcher);
        }
        return ZX_OK;
    }
    case DISK_FORMAT_MINFS: {
        printf("fshost: mounting minfs\n");
        fsck_options_t fsck_options = default_fsck_options;
        if (watcher->CheckFilesystem(device_path, DISK_FORMAT_MINFS, &fsck_options) != ZX_OK) {
            if (FormatMinfs(fd, info) != ZX_OK) {
                return ZX_OK;
            }
        }
        mount_options_t options = default_mount_options;
        MountMinfs(watcher, std::move(fd), &options);
        return ZX_OK;
    }
    case DISK_FORMAT_FAT: {
        // Use the GUID to avoid auto-mounting the EFI partition
        bool efi = gpt_is_efi_guid(guid.value, GPT_GUID_LEN);
        if (efi) {
            printf("fshost: not automounting efi\n");
            return ZX_OK;
        }
        mount_options_t options = default_mount_options;
        options.create_mountpoint = true;
        static int fat_counter = 0;
        char mountpath[FDIO_MAX_FILENAME + 64];
        snprintf(mountpath, sizeof(mountpath), "%s/fat-%d", "/fs" PATH_VOLUME, fat_counter++);
        options.wait_until_ready = false;
        printf("fshost: mounting fatfs\n");
        mount(fd.release(), mountpath, df, &options, LaunchFAT);
        return ZX_OK;
    }
    default:
        return ZX_OK;
    }
}

} // namespace

zx_status_t BlockWatcher::MountData(fbl::unique_fd fd, mount_options_t* options) {
    if (data_mounted_) {
        return ZX_ERR_ALREADY_BOUND;
    }
    options->wait_until_ready = true;

    zx_status_t status =
        mount(fd.release(), "/fs" PATH_DATA, DISK_FORMAT_MINFS, options, LaunchMinfs);
    if (status != ZX_OK) {
        printf("fshost: failed to mount %s: %s.\n", PATH_DATA, zx_status_get_string(status));
    } else {
        data_mounted_ = true;
    }
    return status;
}

zx_status_t BlockWatcher::MountInstall(fbl::unique_fd fd, mount_options_t* options) {
    if (install_mounted_) {
        return ZX_ERR_ALREADY_BOUND;
    }
    options->readonly = true;
    zx_status_t status =
        mount(fd.release(), "/fs" PATH_INSTALL, DISK_FORMAT_MINFS, options, LaunchMinfs);
    if (status != ZX_OK) {
        printf("fshost: failed to mount %s: %s.\n", PATH_INSTALL, zx_status_get_string(status));
    } else {
        install_mounted_ = true;
    }
    return status;
}

zx_status_t BlockWatcher::MountBlob(fbl::unique_fd fd, mount_options_t* options) {
    if (blob_mounted_) {
        return ZX_ERR_ALREADY_BOUND;
    }
    zx_status_t status =
        mount(fd.release(), "/fs" PATH_BLOB, DISK_FORMAT_BLOBFS, options, LaunchBlobfs);
    if (status != ZX_OK) {
        printf("fshost: failed to mount %s: %s.\n", PATH_BLOB, zx_status_get_string(status));
    } else {
        blob_mounted_ = true;
    }
    return status;
}

zx_status_t BlockWatcher::CheckFilesystem(const char* device_path, disk_format_t df,
                                          const fsck_options_t* options) const {
    if (!getenv_bool("zircon.system.filesystem-check", false)) {
        return ZX_OK;
    }

    // TODO(ZX-3793): Blobfs' consistency checker is too slow to execute on boot.
    // With journaling, it is also unnecessary, but would be a nice mechanism for sanity
    // checking.
    if (df == DISK_FORMAT_BLOBFS) {
        fprintf(stderr, "fshost: Skipping blobfs consistency checker\n");
        return ZX_OK;
    }

    zx::ticks before = zx::ticks::now();
    auto timer = fbl::MakeAutoCall([before]() {
        auto after = zx::ticks::now();
        auto duration = fzl::TicksToNs(after - before);
        printf("fshost: fsck took %" PRId64 ".%" PRId64 " seconds\n", duration.to_secs(),
               duration.to_msecs() % 1000);
    });

    printf("fshost: fsck of %s started\n", disk_format_string_[df]);

    auto launch_fsck = [](int argc, const char** argv, zx_handle_t* hnd, uint32_t* ids,
                          size_t len) {
        zx::process proc;
        zx_status_t status = devmgr_launch(*zx::job::default_job(), "fsck", argv,
                                           nullptr, -1, hnd, ids, len, &proc, FS_FOR_FSPROC);
        if (status != ZX_OK) {
            fprintf(stderr, "fshost: Couldn't launch fsck\n");
            return status;
        }
        status = proc.wait_one(ZX_PROCESS_TERMINATED, zx::time::infinite(), nullptr);
        if (status != ZX_OK) {
            fprintf(stderr, "fshost: Error waiting for fsck to terminate\n");
            return status;
        }

        zx_info_process_t info;
        status = proc.get_info(ZX_INFO_PROCESS, &info, sizeof(info), nullptr, nullptr);
        if (status != ZX_OK) {
            fprintf(stderr, "fshost: Failed to get process info\n");
            return status;
        }

        if (info.return_code != 0) {
            fprintf(stderr, "fshost: Fsck return code: %" PRId64 "\n", info.return_code);
            return ZX_ERR_BAD_STATE;
        }
        return ZX_OK;
    };

    zx_status_t status = fsck(device_path, df, options, launch_fsck);
    if (status != ZX_OK) {
        fprintf(stderr, "--------------------------------------------------------------\n");
        fprintf(stderr, "|                                                             \n");
        fprintf(stderr, "|   WARNING: fshost fsck failure!                             \n");
        fprintf(stderr, "|   Corrupt %s @ %s \n", disk_format_string_[df], device_path);
        fprintf(stderr, "|                                                             \n");
        fprintf(stderr, "|   If your system encountered power-loss due to an unclean   \n");
        fprintf(stderr, "|   shutdown, this error was expected. Journaling in minfs    \n");
        fprintf(stderr, "|   is being tracked by ZX-2093. Re-paving will reset your    \n");
        fprintf(stderr, "|   device.                                                   \n");
        fprintf(stderr, "|                                                             \n");
        fprintf(stderr, "|   If your system was shutdown cleanly (via 'dm poweroff'    \n");
        fprintf(stderr, "|   or an OTA), report this device to the local-storage       \n");
        fprintf(stderr, "|   team. Please file bugs with logs before and after reboot. \n");
        fprintf(stderr, "|   Please use the 'filesystem' and 'minfs' component tag.    \n");
        fprintf(stderr, "|                                                             \n");
        fprintf(stderr, "--------------------------------------------------------------\n");
    } else {
        printf("fshost: fsck of %s completed OK\n", disk_format_string_[df]);
    }
    return status;
}

void BlockDeviceWatcher(std::unique_ptr<FsManager> fshost, bool netboot) {
    BlockWatcher watcher(std::move(fshost), netboot);

    fbl::unique_fd dirfd(open("/dev/class/block", O_DIRECTORY | O_RDONLY));
    if (dirfd) {
        fdio_watch_directory(dirfd.get(), BlockDeviceAdded, ZX_TIME_INFINITE, &watcher);
    }
}

} // namespace devmgr
