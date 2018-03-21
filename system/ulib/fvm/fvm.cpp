// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <dirent.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#ifdef __Fuchsia__
#include <fs/mapped-vmo.h>
#include <zircon/syscalls.h>
#include <lib/zx/vmo.h>
#endif

#include <fbl/unique_fd.h>
#include <fbl/unique_ptr.h>
#include <fdio/debug.h>
#include <fdio/watcher.h>
#include <zircon/device/block.h>
#include <zircon/types.h>

#include "fvm/fvm.h"

#define ZXDEBUG 0

namespace {

// Return true if g1 is greater than or equal to g2.
// Safe against integer overflow.
bool generation_ge(uint64_t g1, uint64_t g2) {
    if (g1 == UINT64_MAX && g2 == 0) {
        return false;
    } else if (g1 == 0 && g2 == UINT64_MAX) {
        return true;
    }
    return g1 >= g2;
}

// Validate the metadata's hash value.
// Returns 'true' if it matches, 'false' otherwise.
bool fvm_check_hash(const void* metadata, size_t metadata_size) {
    ZX_DEBUG_ASSERT(metadata_size >= sizeof(fvm::fvm_t));
    const fvm::fvm_t* header = static_cast<const fvm::fvm_t*>(metadata);
    const void* metadata_after_hash =
        reinterpret_cast<const void*>(header->hash + sizeof(header->hash));
    uint8_t empty_hash[sizeof(header->hash)];
    memset(empty_hash, 0, sizeof(empty_hash));

    digest::Digest digest;
    digest.Init();
    digest.Update(metadata, offsetof(fvm::fvm_t, hash));
    digest.Update(empty_hash, sizeof(empty_hash));
    digest.Update(metadata_after_hash,
                  metadata_size - (offsetof(fvm::fvm_t, hash) + sizeof(header->hash)));
    digest.Final();
    return digest == header->hash;
}

#ifdef __Fuchsia__
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
#endif
} // namespace anonymous

void fvm_update_hash(void* metadata, size_t metadata_size) {
    fvm::fvm_t* header = static_cast<fvm::fvm_t*>(metadata);
    memset(header->hash, 0, sizeof(header->hash));
    digest::Digest digest;
    const uint8_t* hash = digest.Hash(metadata, metadata_size);
    memcpy(header->hash, hash, sizeof(header->hash));
}

zx_status_t fvm_validate_header(const void* metadata, const void* backup,
                                size_t metadata_size, const void** out) {
    const fvm::fvm_t* primary_header = static_cast<const fvm::fvm_t*>(metadata);
    const fvm::fvm_t* backup_header = static_cast<const fvm::fvm_t*>(backup);

    bool primary_valid = fvm_check_hash(metadata, metadata_size);
    bool backup_valid = fvm_check_hash(backup, metadata_size);

    // Decide if we should use the primary or the backup copy of metadata
    // for reading.
    bool use_primary;
    if (!primary_valid && !backup_valid) {
        return ZX_ERR_BAD_STATE;
    } else if (primary_valid && !backup_valid) {
        use_primary = true;
    } else if (!primary_valid && backup_valid) {
        use_primary = false;
    } else {
        use_primary = generation_ge(primary_header->generation, backup_header->generation);
    }

    const fvm::fvm_t* header = use_primary ? primary_header : backup_header;
    if (header->magic != FVM_MAGIC) {
        fprintf(stderr, "fvm: Bad magic\n");
        return ZX_ERR_BAD_STATE;
    }
    if (header->version > FVM_VERSION) {
        fprintf(stderr, "fvm: Header Version does not match fvm driver\n");
        return ZX_ERR_BAD_STATE;
    }

    // TODO(smklein): Additional validation....

    if (out) {
        *out = use_primary ? metadata : backup;
    }
    return ZX_OK;
}

#ifdef __Fuchsia__
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

    fbl::unique_ptr<MappedVmo> mvmo;
    zx_status_t status = MappedVmo::Create(metadata_size * 2, "fvm-meta", &mvmo);
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

    xprintf("fvm_init: Success\n");
    xprintf("fvm_init: Slice Count: %zu, size: %zu\n", sb->pslice_count, sb->slice_size);
    xprintf("fvm_init: Vpart offset: %zu, length: %zu\n",
           fvm::kVPartTableOffset, fvm::kVPartTableLength);
    xprintf("fvm_init: Atable offset: %zu, length: %zu\n",
           fvm::kAllocTableOffset, fvm::AllocTableLength(disk_size, slice_size));
    xprintf("fvm_init: Backup meta starts at: %zu\n",
           fvm::BackupStart(disk_size, slice_size));
    xprintf("fvm_init: Slices start at %zu, there are %zu of them\n",
           fvm::SlicesStart(disk_size, slice_size),
           fvm::UsableSlicesCount(disk_size, slice_size));

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
    if ((r = ioctl_block_fvm_alloc(fvm_fd, request)) != ZX_OK) {
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

    xprintf("Destroying partition %s\n", path);
    return static_cast<zx_status_t>(ioctl_block_fvm_destroy(fd.get()));
}
#endif
