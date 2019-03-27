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
#include <fbl/unique_fd.h>
#include <fbl/string_buffer.h>
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
#include <ramdevice-client/ramdisk.h>
#include <zircon/device/block.h>
#include <zircon/processargs.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>
#include <zxcrypt/fdio-volume.h>

#include <utility>

#include "fshost.h"

namespace devmgr {
namespace {

class BlockWatcher {
public:
    BlockWatcher(fbl::unique_ptr<FsManager> fshost, zx::unowned_job job, bool netboot)
        : fshost_(std::move(fshost)), job_(job), netboot_(netboot) {}

    void FuchsiaStart() const { fshost_->FuchsiaStart(); }

    bool IsSystemMounted() const { return fshost_->IsSystemMounted(); }

    zx_status_t InstallFs(const char* path, zx::channel h) {
        return fshost_->InstallFs(path, std::move(h));
    }

    const zx::unowned_job& Job() const { return job_; }

    bool Netbooting() const { return netboot_; }

    // Optionally checks the filesystem stored on the device at |device_path|,
    // if "zircon.system.filesystem-check" is set.
    zx_status_t CheckFilesystem(const char* device_path, disk_format_t df,
                                const fsck_options_t* options) const;

    // Attempts to mount a block device backed by |fd| to "/data".
    // Fails if already mounted.
    zx_status_t MountData(fbl::unique_fd fd, mount_options_t* options);

    // Attempts to mount a block device backed by |fd| to "/install".
    // Fails if already mounted.
    zx_status_t MountInstall(fbl::unique_fd fd, mount_options_t* options);

    // Attempts to mount a block device backed by |fd| to "/blob".
    // Fails if already mounted.
    zx_status_t MountBlob(fbl::unique_fd fd, mount_options_t* options);

private:
    fbl::unique_ptr<FsManager> fshost_;
    zx::unowned_job job_;
    bool netboot_ = false;
    bool data_mounted_ = false;
    bool install_mounted_ = false;
    bool blob_mounted_ = false;
};

// TODO(smklein): When launching filesystems can pass a cookie representing a unique
// BlockWatcher instance, this global should be removed.
zx::unowned_job g_job;

void pkgfs_finish(BlockWatcher* watcher, zx::process proc, zx::channel pkgfs_root) {
    auto deadline = zx::deadline_after(zx::sec(5));
    zx_signals_t observed;
    zx_status_t status =
        proc.wait_one(ZX_USER_SIGNAL_0 | ZX_PROCESS_TERMINATED, deadline, &observed);
    if (status != ZX_OK) {
        printf("fshost: pkgfs did not signal completion: %d (%s)\n", status,
               zx_status_get_string(status));
        return;
    }
    if (!(observed & ZX_USER_SIGNAL_0)) {
        printf("fshost: pkgfs terminated prematurely\n");
        return;
    }
    // re-export /pkgfs/system as /system
    zx::channel systemChan, systemReq;
    if (zx::channel::create(0, &systemChan, &systemReq) != ZX_OK) {
        return;
    }
    if (fdio_open_at(pkgfs_root.get(), "system", FS_DIR_FLAGS, systemReq.release()) != ZX_OK) {
        return;
    }
    // re-export /pkgfs/packages/shell-commands/0/bin as /bin
    zx::channel binChan, binReq;
    if (zx::channel::create(0, &binChan, &binReq) != ZX_OK) {
        return;
    }
    if (fdio_open_at(pkgfs_root.get(), "packages/shell-commands/0/bin", FS_DIR_FLAGS,
                     binReq.release()) != ZX_OK) {
        // non-fatal.
        printf("fshost: failed to install /bin (could not open shell-commands)\n");
    }

    if (watcher->InstallFs("/pkgfs", std::move(pkgfs_root)) != ZX_OK) {
        printf("fshost: failed to install /pkgfs\n");
        return;
    }

    if (watcher->InstallFs("/system", std::move(systemChan)) != ZX_OK) {
        printf("fshost: failed to install /system\n");
        return;
    }

    // as above, failure of /bin export is non-fatal.
    if (watcher->InstallFs("/bin", std::move(binChan)) != ZX_OK) {
        printf("fshost: failed to install /bin\n");
    }

    // start the appmgr
    watcher->FuchsiaStart();
}

// TODO(mcgrathr): Remove this fallback path when the old args
// are no longer used.
void old_launch_blob_init(BlockWatcher* watcher) {
    const char* blob_init = getenv("zircon.system.blob-init");
    if (blob_init == nullptr) {
        return;
    }
    if (watcher->IsSystemMounted()) {
        printf("fshost: zircon.system.blob-init ignored since system already mounted\n");
        return;
    }

    zx::process proc;

    uint32_t type = PA_HND(PA_USER0, 0);
    zx::channel handle, pkgfs_root;
    if (zx::channel::create(0, &handle, &pkgfs_root) != ZX_OK) {
        return;
    }

    // TODO: make blob-init a /fs/blob relative path
    const char* argv[3];
    char binary[strlen(blob_init) + 4];
    sprintf(binary, "/fs%s", blob_init);
    argv[0] = binary;
    const char* blob_init_arg = getenv("zircon.system.blob-init-arg");
    int argc = 1;
    if (blob_init_arg != nullptr) {
        argc++;
        argv[1] = blob_init_arg;
    }
    argv[argc] = nullptr;

    const zx_handle_t raw_handle = handle.release();
    zx_status_t status =
        devmgr_launch(*watcher->Job(), "pkgfs", argv,
                      nullptr, -1, &raw_handle, &type, 1, &proc, FS_DATA | FS_BLOB | FS_SVC);

    if (status != ZX_OK) {
        printf("fshost: '%s' failed to launch: %d\n", blob_init, status);
        return;
    }

    pkgfs_finish(watcher, std::move(proc), std::move(pkgfs_root));
}

// Launching pkgfs uses its own loader service and command lookup to run out of
// the blobfs without any real filesystem.  Files are found by
// getenv("zircon.system.pkgfs.file.PATH") returning a blob content ID.
// That is, a manifest of name->blob is embedded in /boot/config/devmgr.
zx_status_t pkgfs_ldsvc_load_blob(void* ctx, const char* prefix, const char* name,
                                  zx_handle_t* vmo) {
    const int fs_blob_fd = static_cast<int>(reinterpret_cast<intptr_t>(ctx));
    char key[256];
    if (snprintf(key, sizeof(key), "zircon.system.pkgfs.file.%s%s", prefix, name) >=
        (int)sizeof(key)) {
        return ZX_ERR_BAD_PATH;
    }
    const char* blob = getenv(key);
    if (blob == nullptr) {
        return ZX_ERR_NOT_FOUND;
    }
    int fd = openat(fs_blob_fd, blob, O_RDONLY);
    if (fd < 0) {
        return ZX_ERR_NOT_FOUND;
    }
    zx_status_t status = fdio_get_vmo_clone(fd, vmo);
    close(fd);
    if (status == ZX_OK) {
        zx_object_set_property(*vmo, ZX_PROP_NAME, key, strlen(key));
    }
    return status;
}

zx_status_t pkgfs_ldsvc_load_object(void* ctx, const char* name, zx_handle_t* vmo) {
    return pkgfs_ldsvc_load_blob(ctx, "lib/", name, vmo);
}

zx_status_t pkgfs_ldsvc_load_abspath(void* ctx, const char* name, zx_handle_t* vmo) {
    return pkgfs_ldsvc_load_blob(ctx, "", name + 1, vmo);
}

zx_status_t pkgfs_ldsvc_publish_data_sink(void* ctx, const char* name, zx_handle_t vmo) {
    zx_handle_close(vmo);
    return ZX_ERR_NOT_SUPPORTED;
}

void pkgfs_ldsvc_finalizer(void* ctx) {
    close(static_cast<int>(reinterpret_cast<intptr_t>(ctx)));
}

const loader_service_ops_t pkgfs_ldsvc_ops = {
    .load_object = pkgfs_ldsvc_load_object,
    .load_abspath = pkgfs_ldsvc_load_abspath,
    .publish_data_sink = pkgfs_ldsvc_publish_data_sink,
    .finalizer = pkgfs_ldsvc_finalizer,
};

// Create a local loader service with a fixed mapping of names to blobs.
zx_status_t pkgfs_ldsvc_start(fbl::unique_fd fs_blob_fd, zx::channel* ldsvc) {
    loader_service_t* service;
    zx_status_t status =
        loader_service_create(nullptr, &pkgfs_ldsvc_ops,
                              reinterpret_cast<void*>(static_cast<intptr_t>(fs_blob_fd.get())),
                              &service);
    if (status != ZX_OK) {
        printf("fshost: cannot create pkgfs loader service: %d (%s)\n", status,
               zx_status_get_string(status));
        return status;
    }
    // The loader service now owns this FD
    __UNUSED auto fd = fs_blob_fd.release();

    status = loader_service_connect(service, ldsvc->reset_and_get_address());
    loader_service_release(service);
    if (status != ZX_OK) {
        printf("fshost: cannot connect pkgfs loader service: %d (%s)\n", status,
               zx_status_get_string(status));
    }
    return status;
}

bool pkgfs_launch(BlockWatcher* watcher) {
    const char* cmd = getenv("zircon.system.pkgfs.cmd");
    if (cmd == nullptr) {
        return false;
    }

    fbl::unique_fd fs_blob_fd(open("/fs/blob", O_RDONLY | O_DIRECTORY));
    if (!fs_blob_fd) {
        printf("fshost: open(/fs/blob): %m\n");
        return false;
    }

    zx::channel h0, h1;
    zx_status_t status = zx::channel::create(0, &h0, &h1);
    if (status != ZX_OK) {
        printf("fshost: cannot create pkgfs root channel: %d (%s)\n", status,
               zx_status_get_string(status));
        return false;
    }

    auto args = ArgumentVector::FromCmdline(cmd);
    auto argv = args.argv();
    // Remove leading slashes before asking pkgfs_ldsvc_load_blob to load the
    // file.
    const char* file = argv[0];
    while (file[0] == '/') {
        ++file;
    }
    zx::vmo executable;
    status = pkgfs_ldsvc_load_blob(reinterpret_cast<void*>(static_cast<intptr_t>(fs_blob_fd.get())),
                                   "", argv[0], executable.reset_and_get_address());
    if (status != ZX_OK) {
        printf("fshost: cannot load pkgfs executable: %d (%s)\n", status,
               zx_status_get_string(status));
        return false;
    }

    zx::channel loader;
    status = pkgfs_ldsvc_start(std::move(fs_blob_fd), &loader);
    if (status != ZX_OK) {
        printf("fshost: cannot pkgfs loader: %d (%s)\n", status, zx_status_get_string(status));
        return false;
    }

    const zx_handle_t raw_h1 = h1.release();
    zx::process proc;
    args.Print("fshost");
    status = devmgr_launch_with_loader(*watcher->Job(), "pkgfs",
                                       std::move(executable), std::move(loader),
                                       argv, nullptr, -1, &raw_h1,
                                       (const uint32_t[]){PA_HND(PA_USER0, 0)}, 1, &proc,
                                       FS_DATA | FS_BLOB | FS_SVC);
    if (status != ZX_OK) {
        printf("fshost: failed to launch %s: %d (%s)\n", cmd, status, zx_status_get_string(status));
        return false;
    }

    pkgfs_finish(watcher, std::move(proc), std::move(h0));
    return true;
}

void launch_blob_init(BlockWatcher* watcher) {
    if (!pkgfs_launch(watcher)) {
        // TODO(mcgrathr): Remove when the old args are no longer used.
        old_launch_blob_init(watcher);
    }
}

zx_status_t launch_blobfs(int argc, const char** argv, zx_handle_t* hnd, uint32_t* ids,
                          size_t len) {
    return devmgr_launch(*g_job, "blobfs:/blob", argv, nullptr,
                         -1, hnd, ids, len, nullptr, FS_FOR_FSPROC);
}

zx_status_t launch_minfs(int argc, const char** argv, zx_handle_t* hnd, uint32_t* ids, size_t len) {
    return devmgr_launch(*g_job, "minfs:/data", argv, nullptr,
                         -1, hnd, ids, len, nullptr, FS_FOR_FSPROC);
}

zx_status_t launch_fat(int argc, const char** argv, zx_handle_t* hnd, uint32_t* ids, size_t len) {
    return devmgr_launch(*g_job, "fatfs:/volume", argv, nullptr,
                         -1, hnd, ids, len, nullptr, FS_FOR_FSPROC);
}

zx_status_t BlockWatcher::MountData(fbl::unique_fd fd, mount_options_t* options) {
    if (data_mounted_) {
        return ZX_ERR_ALREADY_BOUND;
    }
    options->wait_until_ready = true;

    zx_status_t status =
        mount(fd.release(), "/fs" PATH_DATA, DISK_FORMAT_MINFS, options, launch_minfs);
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
        mount(fd.release(), "/fs" PATH_INSTALL, DISK_FORMAT_MINFS, options, launch_minfs);
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
        mount(fd.release(), "/fs" PATH_BLOB, DISK_FORMAT_BLOBFS, options, launch_blobfs);
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
        printf("fshost: fsck took %" PRId64".%" PRId64 " seconds\n", duration.to_secs(),
               duration.to_msecs() % 1000);
    });

    printf("fshost: fsck of %s started\n", disk_format_string_[df]);

    auto launch_fsck = [](int argc, const char** argv, zx_handle_t* hnd, uint32_t* ids,
                          size_t len) {
        zx::process proc;
        zx_status_t status = devmgr_launch(*g_job, "fsck", argv,
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
        fprintf(stderr, "---------------------------------------------------------\n");
        fprintf(stderr, "|                                                        \n");
        fprintf(stderr, "|   WARNING: fshost fsck failure!                        \n");
        fprintf(stderr, "|   Corrupt device: %s \n", device_path);
        fprintf(stderr, "|   Please report this device to the local storage team, \n");
        fprintf(stderr, "|   Preferably BEFORE reformatting your device.          \n");
        fprintf(stderr, "|                                                        \n");
        fprintf(stderr, "---------------------------------------------------------\n");
    } else {
        printf("fshost: fsck of %s completed OK\n", disk_format_string_[df]);
    }
    return status;
}

// Attempt to mount the device pointed to be the file descriptor at a known
// location.
//
// Returns ZX_ERR_ALREADY_BOUND if the device could be mounted, but something
// is already mounted at that location. Returns ZX_ERR_INVALID_ARGS if the
// GUID of the device does not match a known valid one. Returns
// ZX_ERR_NOT_SUPPORTED if the GUID is a system GUID. Returns ZX_OK if an
// attempt to mount is made, without checking mount success.
zx_status_t mount_minfs(BlockWatcher* watcher, fbl::unique_fd fd, mount_options_t* options) {
    fuchsia_hardware_block_partition_GUID type_guid;
    {
        fzl::UnownedFdioCaller disk_connection(fd.get());
        zx::unowned_channel channel(disk_connection.borrow_channel());
        zx_status_t io_status, status;
        io_status = fuchsia_hardware_block_partition_PartitionGetTypeGuid(channel->get(), &status,
                                                                          &type_guid);
        if (io_status != ZX_OK) return io_status;
        if (status != ZX_OK) return status;
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
int unseal_zxcrypt_threadfunc(void* arg) {
    fbl::unique_ptr<int> fd_ptr(static_cast<int*>(arg));
    fbl::unique_fd fd(*fd_ptr);

    zx_status_t rc;
    fbl::unique_ptr<zxcrypt::FdioVolume> zxcrypt_volume;
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
    // TODO(security): ZX-2670 we should call a separate binary to
    // key this with a real key
    uint8_t null_key[zxcrypt::kZx1130KeyLen];
    memset(null_key, 0, zxcrypt::kZx1130KeyLen);
    uint8_t slot = 0;
    if ((rc = zxcrypt_volume_manager.Unseal(null_key, zxcrypt::kZx1130KeyLen, slot)) != ZX_OK) {
        printf("fshost: couldn't unseal zxcrypt manager device");
        return 0;
    }

    return 0;
}

zx_status_t block_device_added(int dirfd, int event, const char* name, void* cookie) {
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
    fuchsia_hardware_block_partition_GUID guid;
    {
        fzl::UnownedFdioCaller disk_connection(fd.get());
        zx::unowned_channel disk(disk_connection.borrow_channel());

        fuchsia_hardware_block_BlockInfo info;
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
                int err = thrd_create_with_name(&th, &unseal_zxcrypt_threadfunc, raw_fd_ptr, "zxcrypt-unseal");
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
            mount_minfs(watcher, std::move(fd), &options);
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
            launch_blob_init(watcher);
        }
        return ZX_OK;
    }
    case DISK_FORMAT_MINFS: {
        printf("fshost: mounting minfs\n");
        fsck_options_t fsck_options = default_fsck_options;
        if (watcher->CheckFilesystem(device_path, DISK_FORMAT_MINFS, &fsck_options) != ZX_OK) {
            return ZX_OK;
        }
        mount_options_t options = default_mount_options;
        mount_minfs(watcher, std::move(fd), &options);
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
        mount(fd.release(), mountpath, df, &options, launch_fat);
        return ZX_OK;
    }
    default:
        return ZX_OK;
    }
}

} // namespace

void block_device_watcher(fbl::unique_ptr<FsManager> fshost, zx::unowned_job job, bool netboot) {
    g_job = job;
    BlockWatcher watcher(std::move(fshost), std::move(job), netboot);

    fbl::unique_fd dirfd(open("/dev/class/block", O_DIRECTORY | O_RDONLY));
    if (dirfd) {
        fdio_watch_directory(dirfd.get(), block_device_added, ZX_TIME_INFINITE, &watcher);
    }
}

} // namespace devmgr
