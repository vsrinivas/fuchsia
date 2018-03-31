// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fs-management/mount.h>

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>

#include <fbl/alloc_checker.h>
#include <fbl/auto_call.h>
#include <fbl/type_support.h>
#include <fbl/unique_fd.h>
#include <fbl/unique_ptr.h>
#include <fdio/limits.h>
#include <fdio/util.h>
#include <fdio/vfs.h>
#include <fs/client.h>
#include <zircon/compiler.h>
#include <zircon/device/vfs.h>
#include <zircon/processargs.h>
#include <zircon/syscalls.h>

namespace {

using fbl::unique_fd;

void UnmountHandle(zx_handle_t root, bool wait_until_ready) {
    // We've entered a failure case where the filesystem process (which may or may not be alive)
    // had a *chance* to be spawned, but cannot be attached to a vnode (for whatever reason).
    // Rather than abandoning the filesystem process (maybe causing dirty bits to be set), give it a
    // chance to shutdown properly.
    //
    // The unmount process is a little atypical, since we're just sending a signal over a handle,
    // rather than detaching the mounted filesytem from the "parent" filesystem.
    vfs_unmount_handle(root, wait_until_ready ? ZX_TIME_INFINITE : 0);
}

// Performs the actual work of mounting a volume.
class Mounter {
public:
    // The mount point is either a path (to be created) or an existing fd.
    explicit Mounter(int fd) : path_(nullptr), fd_(fd) {}
    explicit Mounter(const char* path) : path_(path), fd_(-1) {}
    ~Mounter() {}

    // Mounts the given device.
    zx_status_t Mount(unique_fd device, disk_format_t format, const mount_options_t& options,
                      LaunchCallback cb);

    DISALLOW_COPY_AND_ASSIGN_ALLOW_MOVE(Mounter);
private:
    zx_status_t PrepareHandles(unique_fd device);
    zx_status_t MakeDirAndMount(const mount_options_t& options);
    zx_status_t LaunchAndMount(LaunchCallback cb, const mount_options_t& options,
                               const char** argv, int argc);
    zx_status_t MountNativeFs(const char* binary, unique_fd device,
                              const mount_options_t& options, LaunchCallback cb);
    zx_status_t MountFat(unique_fd device, const mount_options_t& options,
                         LaunchCallback cb);

    zx_handle_t root_ = ZX_HANDLE_INVALID;
    const char* path_;
    int fd_;
    uint32_t flags_ = 0;  // Currently not used.
    size_t num_handles_ = 0;
    zx_handle_t handles_[FDIO_MAX_HANDLES * 2];
    uint32_t ids_[FDIO_MAX_HANDLES * 2];
};

// Initializes 'handles_' and 'ids_' with the root handle and block device handle.
zx_status_t Mounter::PrepareHandles(unique_fd device) {
    zx_handle_t mountee_handle;
    zx_status_t status = zx_channel_create(0, &mountee_handle, &root_);
    if (status != ZX_OK) {
        return status;
    }
    handles_[0] = mountee_handle;
    ids_[0] = PA_USER0;
    num_handles_ = 1;

    int device_fd = device.release();
    status = fdio_transfer_fd(device_fd, FS_FD_BLOCKDEVICE, &handles_[1], &ids_[1]);
    if (status < 0) {
        // Note that fdio_transfer_fd returns > 0 on success :(.
        fprintf(stderr, "Failed to access device handle\n");
        zx_handle_close(mountee_handle);
        zx_handle_close(root_);
        device.reset(device_fd);
        return status != 0 ? status : ZX_ERR_BAD_STATE;
    }
    num_handles_ += status;
    return ZX_OK;
}

zx_status_t Mounter::MakeDirAndMount(const mount_options_t& options) {
    auto cleanup = fbl::MakeAutoCall([this, options]() {
        UnmountHandle(root_, options.wait_until_ready);
    });

    // Open the parent path as O_ADMIN, and sent the mkdir+mount command
    // to that directory.
    char parent_path[PATH_MAX];
    const char* name;
    strcpy(parent_path, path_);
    char *last_slash = strrchr(parent_path, '/');
    if (last_slash == NULL) {
        strcpy(parent_path, ".");
        name = path_;
    } else {
        *last_slash = '\0';
        name = last_slash + 1;
        if (*name == '\0') {
            return ZX_ERR_INVALID_ARGS;
        }
    }

    unique_fd parent(open(parent_path, O_RDONLY | O_DIRECTORY | O_ADMIN));
    if (!parent) {
        return ZX_ERR_IO;
    }

    size_t config_size = sizeof(mount_mkdir_config_t) + strlen(name) + 1;
    fbl::AllocChecker checker;
    fbl::unique_ptr<char[]> config_buffer(new(&checker) char[config_size]);
    mount_mkdir_config_t* config = reinterpret_cast<mount_mkdir_config_t*>(config_buffer.get());
    if (!checker.check()) {
        return ZX_ERR_NO_MEMORY;
    }
    config->fs_root = root_;
    config->flags = flags_;
    strcpy(config->name, name);

    cleanup.cancel();

    // Ioctl will close root for us if an error occurs
    return static_cast<zx_status_t>(ioctl_vfs_mount_mkdir_fs(parent.get(), config, config_size));
}

// Calls the 'launch callback' and mounts the remote handle to the target vnode, if successful.
zx_status_t Mounter::LaunchAndMount(LaunchCallback cb, const mount_options_t& options,
                                    const char** argv, int argc) {
    auto cleanup = fbl::MakeAutoCall([this, options]() {
        UnmountHandle(root_, options.wait_until_ready);
    });

    zx_status_t status = cb(argc, argv, handles_, ids_, num_handles_);
    if (status != ZX_OK) {
        return status;
    }

    if (options.wait_until_ready) {
        // Wait until the filesystem is ready to take incoming requests
        zx_signals_t observed;
        status = zx_object_wait_one(root_, ZX_USER_SIGNAL_0 | ZX_CHANNEL_PEER_CLOSED,
                                    ZX_TIME_INFINITE, &observed);
        if ((status != ZX_OK) || (observed & ZX_CHANNEL_PEER_CLOSED)) {
            status = (status != ZX_OK) ? status : ZX_ERR_BAD_STATE;
            return status;
        }
    }
    cleanup.cancel();

    // Install remote handle.
    if (options.create_mountpoint) {
        return MakeDirAndMount(options);
    }
    // Ioctl will close root for us if an error occurs
    return static_cast<zx_status_t>(ioctl_vfs_mount_fs(fd_, &root_));
}

zx_status_t Mounter::MountNativeFs(const char* binary, unique_fd device,
                                   const mount_options_t& options, LaunchCallback cb) {
    zx_status_t status = PrepareHandles(fbl::move(device));
    if (status != ZX_OK) {
        return status;
    }

    if (options.verbose_mount) {
        printf("fs_mount: Launching %s\n", binary);
    }

    // 1. binary
    // 2. (optional) readonly
    // 3. (optional) verbose
    // 4. (optional) metrics
    // 5. command
    const char* argv[5] = {binary};
    int argc = 1;
    if (options.readonly) {
        argv[argc++] = "--readonly";
    }
    if (options.verbose_mount) {
        argv[argc++] = "--verbose";
    }
    if (options.collect_metrics) {
        argv[argc++] = "--metrics";
    }
    argv[argc++] = "mount";
    return LaunchAndMount(cb, options, argv, argc);
}

zx_status_t Mounter::MountFat(unique_fd device, const mount_options_t& options,
                              LaunchCallback cb) {
    zx_status_t status = PrepareHandles(fbl::move(device));
    if (status != ZX_OK) {
        return status;
    }

    char readonly_arg[64];
    snprintf(readonly_arg, sizeof(readonly_arg), "-readonly=%s",
             options.readonly ? "true" : "false");
    char blockfd_arg[64];
    snprintf(blockfd_arg, sizeof(blockfd_arg), "-blockFD=%d", FS_FD_BLOCKDEVICE);

    if (options.verbose_mount) {
        printf("fs_mount: Launching ThinFS\n");
    }
    const char* argv[] = {
        "/system/bin/thinfs",
        readonly_arg,
        blockfd_arg,
        "mount",
    };
    return LaunchAndMount(cb, options, argv, countof(argv));
}

zx_status_t Mounter::Mount(unique_fd device, disk_format_t format,
                           const mount_options_t& options, LaunchCallback cb) {
    switch (format) {
    case DISK_FORMAT_MINFS:
        return MountNativeFs("/boot/bin/minfs", fbl::move(device), options, cb);
    case DISK_FORMAT_BLOBFS:
        return MountNativeFs("/boot/bin/blobfs", fbl::move(device), options, cb);
    case DISK_FORMAT_FAT:
        return MountFat(fbl::move(device), options, cb);
    default:
        return ZX_ERR_NOT_SUPPORTED;
    }
}

}  // namespace

const mount_options_t default_mount_options = {
    .readonly = false,
    .verbose_mount = false,
    .collect_metrics = false,
    .wait_until_ready = true,
    .create_mountpoint = false,
};

const mkfs_options_t default_mkfs_options = {
    .verbose = false,
};

const fsck_options_t default_fsck_options = {
    .verbose = false,
    .never_modify = false,
    .always_modify = false,
    .force = false,
};

disk_format_t detect_disk_format(int fd) {
    uint8_t data[HEADER_SIZE];
    if (read(fd, data, sizeof(data)) != sizeof(data)) {
        fprintf(stderr, "Error reading block device\n");
        return DISK_FORMAT_UNKNOWN;
    }

    if (!memcmp(data, fvm_magic, sizeof(fvm_magic))) {
        return DISK_FORMAT_FVM;
    }

    if (!memcmp(data + 0x200, gpt_magic, sizeof(gpt_magic))) {
        return DISK_FORMAT_GPT;
    }

    if (!memcmp(data, minfs_magic, sizeof(minfs_magic))) {
        return DISK_FORMAT_MINFS;
    }

    if (!memcmp(data, blobfs_magic, sizeof(blobfs_magic))) {
        return DISK_FORMAT_BLOBFS;
    }

    if ((data[510] == 0x55 && data[511] == 0xAA)) {
        if ((data[38] == 0x29 || data[66] == 0x29)) {
            // 0x55AA are always placed at offset 510 and 511 for FAT filesystems.
            // 0x29 is the Boot Signature, but it is placed at either offset 38 or
            // 66 (depending on FAT type).
            return DISK_FORMAT_FAT;
        }
        return DISK_FORMAT_MBR;
    }
    return DISK_FORMAT_UNKNOWN;
}

zx_status_t fmount(int device_fd, int mount_fd, disk_format_t df,
                   const mount_options_t* options, LaunchCallback cb) {
    Mounter mounter(mount_fd);
    return mounter.Mount(unique_fd(device_fd), df, *options, cb);
}

zx_status_t mount(int device_fd, const char* mount_path, disk_format_t df,
                  const mount_options_t* options, LaunchCallback cb) {
    if (!options->create_mountpoint) {
        // Open mountpoint; use it directly.
        unique_fd mount_point(open(mount_path, O_RDONLY | O_DIRECTORY | O_ADMIN));
        if (!mount_point) {
            return ZX_ERR_BAD_STATE;
        }
        return fmount(device_fd, mount_point.get(), df, options, cb);
    }

    Mounter mounter(mount_path);
    return mounter.Mount(unique_fd(device_fd), df, *options, cb);
}

zx_status_t fumount(int mount_fd) {
    zx_handle_t h;
    zx_status_t status = static_cast<zx_status_t>(ioctl_vfs_unmount_node(mount_fd, &h));
    if (status < 0) {
        fprintf(stderr, "Could not unmount filesystem: %d\n", status);
    } else {
        status = vfs_unmount_handle(h, ZX_TIME_INFINITE);
    }
    return status;
}

zx_status_t umount(const char* mount_path) {
    unique_fd fd(open(mount_path, O_DIRECTORY | O_NOREMOTE | O_ADMIN));
    if (!fd) {
        fprintf(stderr, "Could not open directory: %s\n", strerror(errno));
        return ZX_ERR_BAD_STATE;
    }
    zx_status_t status = fumount(fd.get());
    return status;
}
