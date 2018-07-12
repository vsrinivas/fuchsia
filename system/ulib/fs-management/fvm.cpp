// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fs-management/mount.h>

#include <errno.h>
#include <dirent.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>

#include <fbl/alloc_checker.h>
#include <fbl/auto_call.h>
#include <fbl/type_support.h>
#include <fbl/unique_fd.h>
#include <fbl/unique_ptr.h>
#include <fs-management/fvm.h>
#include <fvm/fvm.h>
#include <lib/fdio/limits.h>
#include <lib/fdio/util.h>
#include <lib/fdio/vfs.h>
#include <lib/fdio/watcher.h>
#include <lib/fzl/mapped-vmo.h>
#include <fs/client.h>
#include <zircon/compiler.h>
#include <zircon/device/block.h>
#include <zircon/device/vfs.h>
#include <zircon/processargs.h>
#include <zircon/syscalls.h>

namespace {
// Checks that |fd| is a partition which matches |uniqueGUID| and |typeGUID|.
// If either is null, it doesn't compare |fd| with that guid.
// At least one of the GUIDs must be non-null.
static bool is_partition(int fd, const uint8_t* uniqueGUID, const uint8_t* typeGUID) {
    ZX_ASSERT(uniqueGUID || typeGUID);
    uint8_t buf[GUID_LEN];
    if (fd < 0) {
        return false;
    }
    if (typeGUID) {
        if (ioctl_block_get_type_guid(fd, buf, sizeof(buf)) < 0) {
            return false;
        } else if (memcmp(buf, typeGUID, GUID_LEN) != 0) {
            return false;
        }
    }
    if (uniqueGUID) {
        if (ioctl_block_get_partition_guid(fd, buf, sizeof(buf)) < 0) {
            return false;
        } else if (memcmp(buf, uniqueGUID, GUID_LEN) != 0) {
            return false;
        }
    }
    return true;
}

constexpr char kBlockDevPath[] = "/dev/class/block/";

}  // namespace anonymous

zx_status_t fvm_init(int fd, size_t slice_size) {
    if (slice_size % FVM_BLOCK_SIZE != 0) {
        // Alignment
        return ZX_ERR_INVALID_ARGS;
    } else if ((slice_size * VSLICE_MAX) / VSLICE_MAX != slice_size) {
        // Overflow
        return ZX_ERR_INVALID_ARGS;
    }

    // The metadata layout of the FVM is dependent on the
    // size of the FVM's underlying partition.
    block_info_t block_info;
    ssize_t rc = ioctl_block_get_info(fd, &block_info);

    if (rc < 0) {
        return static_cast<zx_status_t>(rc);
    } else if (rc != sizeof(block_info)) {
        return ZX_ERR_BAD_STATE;
    } else if (slice_size == 0 || slice_size % block_info.block_size) {
        return ZX_ERR_BAD_STATE;
    }

    size_t disk_size = block_info.block_count * block_info.block_size;
    size_t metadata_size = fvm::MetadataSize(disk_size, slice_size);

    fbl::unique_ptr<fzl::MappedVmo> mvmo;
    zx_status_t status = fzl::MappedVmo::Create(metadata_size * 2, "fvm-meta", &mvmo);
    if (status != ZX_OK) {
        return status;
    }

    // Clear entire primary copy of metadata
    memset(mvmo->GetData(), 0, metadata_size);

    // Superblock
    fvm::fvm_t* sb = static_cast<fvm::fvm_t*>(mvmo->GetData());
    sb->magic = FVM_MAGIC;
    sb->version = FVM_VERSION;
    sb->pslice_count = (disk_size - metadata_size * 2) / slice_size;
    sb->slice_size = slice_size;
    sb->fvm_partition_size = disk_size;
    sb->vpartition_table_size = fvm::kVPartTableLength;
    sb->allocation_table_size = fvm::AllocTableLength(disk_size, slice_size);
    sb->generation = 0;

    if (sb->pslice_count == 0) {
        return ZX_ERR_NO_SPACE;
    }

    fvm_update_hash(mvmo->GetData(), metadata_size);

    const void* backup = reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(mvmo->GetData()) +
                                                 metadata_size);
    status = fvm_validate_header(mvmo->GetData(), backup, metadata_size, nullptr);
    if (status != ZX_OK) {
        return status;
    }

    if (lseek(fd, 0, SEEK_SET) < 0) {
        return ZX_ERR_BAD_STATE;
    }
    // Write to primary copy.
    if (write(fd, mvmo->GetData(), metadata_size) != static_cast<ssize_t>(metadata_size)) {
        return ZX_ERR_BAD_STATE;
    }
    // Write to secondary copy, to overwrite any previous FVM metadata copy that
    // could be here.
    if (write(fd, mvmo->GetData(), metadata_size) != static_cast<ssize_t>(metadata_size)) {
        return ZX_ERR_BAD_STATE;
    }

    return ZX_OK;
}

// Helper function to overwrite FVM given the slice_size
zx_status_t fvm_overwrite(const char* path, size_t slice_size) {
    int fd = open(path, O_RDWR);

    if (fd <= 0) {
        fprintf(stderr, "fvm_destroy: Failed to open block device\n");
        return -1;
    }

    block_info_t block_info;
    ssize_t rc = ioctl_block_get_info(fd, &block_info);

    if (rc < 0 || rc != sizeof(block_info)) {
        printf("fvm_destroy: Failed to query block device\n");
        return -1;
    }

    size_t disk_size = block_info.block_count * block_info.block_size;
    size_t metadata_size = fvm::MetadataSize(disk_size, slice_size);

    fbl::AllocChecker ac;
    fbl::unique_ptr<uint8_t[]> buf(new (&ac) uint8_t[metadata_size]);
    if (!ac.check()) {
        printf("fvm_destroy: Failed to allocate buffer\n");
        return -1;
    }

    memset(buf.get(), 0, metadata_size);

    if (lseek(fd, 0, SEEK_SET) < 0) {
        return -1;
    }

    // Write to primary copy.
    if (write(fd, buf.get(), metadata_size) != static_cast<ssize_t>(metadata_size)) {
        return -1;
    }

    // Write to backup copy
    if (write(fd, buf.get(), metadata_size) != static_cast<ssize_t>(metadata_size)) {
       return -1;
    }

    if (ioctl_block_rr_part(fd) != 0) {
        return -1;
    }

    close(fd);
    return ZX_OK;
}

// Helper function to destroy FVM
zx_status_t fvm_destroy(const char* path) {
    char driver_path[PATH_MAX];
    if (strlcpy(driver_path, path, sizeof(driver_path)) >= sizeof(driver_path)) {
        return ZX_ERR_BAD_PATH;
    }
    if (strlcat(driver_path, "/fvm", sizeof(driver_path)) >= sizeof(driver_path)) {
        return ZX_ERR_BAD_PATH;
    }
    fbl::unique_fd driver_fd(open(driver_path, O_RDWR));

    if (!driver_fd) {
        fprintf(stderr, "fvm_destroy: Failed to open fvm driver: %s\n", driver_path);
        return -1;
    }

    fvm_info_t fvm_info;
    ssize_t r;
    if ((r = ioctl_block_fvm_query(driver_fd.get(), &fvm_info)) <= 0) {
        fprintf(stderr, "fvm_destroy: Failed to query fvm: %ld\n", r);
        return -1;
    }

    return fvm_overwrite(path, fvm_info.slice_size);
}

// Helper function to allocate, find, and open VPartition.
int fvm_allocate_partition(int fvm_fd, const alloc_req_t* request) {
    ssize_t r;
    if ((r = ioctl_block_fvm_alloc_partition(fvm_fd, request)) != ZX_OK) {
        return -1;
    }

    return open_partition(request->guid, request->type, ZX_SEC(10), nullptr);
}

int open_partition(const uint8_t* uniqueGUID, const uint8_t* typeGUID,
                   zx_duration_t timeout, char* out_path) {
    ZX_ASSERT(uniqueGUID || typeGUID);

    typedef struct {
        const uint8_t* guid;
        const uint8_t* type;
        char* out_path;
        fbl::unique_fd out_partition;
    } alloc_helper_info_t;

    alloc_helper_info_t info;
    info.guid = uniqueGUID;
    info.type = typeGUID;
    info.out_path = out_path;
    info.out_partition.reset();

    auto cb = [](int dirfd, int event, const char* fn, void* cookie) {
        if (event != WATCH_EVENT_ADD_FILE) {
            return ZX_OK;
        } else if ((strcmp(fn, ".") == 0) || strcmp(fn, "..") == 0) {
            return ZX_OK;
        }
        auto info = static_cast<alloc_helper_info_t*>(cookie);
        fbl::unique_fd devfd(openat(dirfd, fn, O_RDWR));
        if (!devfd) {
            return ZX_OK;
        }
        if (is_partition(devfd.get(), info->guid, info->type)) {
            info->out_partition = fbl::move(devfd);
            if (info->out_path) {
                strcpy(info->out_path, kBlockDevPath);
                strcat(info->out_path, fn);
            }
            return ZX_ERR_STOP;
        }
        return ZX_OK;
    };

    DIR* dir = opendir(kBlockDevPath);
    if (dir == nullptr) {
        return -1;
    }

    zx_time_t deadline = zx_deadline_after(timeout);
    if (fdio_watch_directory(dirfd(dir), cb, deadline, &info) != ZX_ERR_STOP) {
        return -1;
    }
    closedir(dir);
    return info.out_partition.release();
}

zx_status_t destroy_partition(const uint8_t* uniqueGUID, const uint8_t* typeGUID) {
    char path[PATH_MAX];
    fbl::unique_fd fd(open_partition(uniqueGUID, typeGUID, 0, path));

    if (!fd) {
        return ZX_ERR_IO;
    }

    return static_cast<zx_status_t>(ioctl_block_fvm_destroy_partition(fd.get()));
}
