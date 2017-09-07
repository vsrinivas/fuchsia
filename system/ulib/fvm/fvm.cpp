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
#include <unistd.h>

#include <fs/mapped-vmo.h>
#include <magenta/device/block.h>
#include <magenta/syscalls.h>
#include <magenta/types.h>
#include <mxio/debug.h>
#include <mxio/watcher.h>
#include <fbl/unique_ptr.h>

#include "fvm/fvm.h"

#define MXDEBUG 0

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
    MX_DEBUG_ASSERT(metadata_size >= sizeof(fvm::fvm_t));
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

static bool is_partition(int fd, const uint8_t* uniqueGUID, const uint8_t* typeGUID) {
    uint8_t buf[GUID_LEN];
    if (fd < 0) {
        return false;
    } else if (ioctl_block_get_type_guid(fd, buf, sizeof(buf)) < 0) {
        return false;
    } else if (memcmp(buf, typeGUID, GUID_LEN) != 0) {
        return false;
    } else if (ioctl_block_get_partition_guid(fd, buf, sizeof(buf)) < 0) {
        return false;
    } else if (memcmp(buf, uniqueGUID, GUID_LEN) != 0) {
        return false;
    }
    return true;
}

constexpr char kBlockDevPath[] = "/dev/class/block/";

} // namespace anonymous

void fvm_update_hash(void* metadata, size_t metadata_size) {
    fvm::fvm_t* header = static_cast<fvm::fvm_t*>(metadata);
    memset(header->hash, 0, sizeof(header->hash));
    digest::Digest digest;
    const uint8_t* hash = digest.Hash(metadata, metadata_size);
    memcpy(header->hash, hash, sizeof(header->hash));
}

mx_status_t fvm_validate_header(const void* metadata, const void* backup,
                                size_t metadata_size, const void** out) {
    const fvm::fvm_t* primary_header = static_cast<const fvm::fvm_t*>(metadata);
    const fvm::fvm_t* backup_header = static_cast<const fvm::fvm_t*>(backup);

    bool primary_valid = fvm_check_hash(metadata, metadata_size);
    bool backup_valid = fvm_check_hash(backup, metadata_size);

    // Decide if we should use the primary or the backup copy of metadata
    // for reading.
    bool use_primary;
    if (!primary_valid && !backup_valid) {
        fprintf(stderr, "fvm: Neither copy of metadata is valid\n");
        return MX_ERR_BAD_STATE;
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
        return MX_ERR_BAD_STATE;
    }
    if (header->version > FVM_VERSION) {
        fprintf(stderr, "fvm: Header Version does not match fvm driver\n");
        return MX_ERR_BAD_STATE;
    }

    // TODO(smklein): Additional validation....

    if (out) {
        *out = use_primary ? metadata : backup;
    }
    return MX_OK;
}

mx_status_t fvm_init(int fd, size_t slice_size) {
    if (slice_size % FVM_BLOCK_SIZE != 0) {
        return MX_ERR_INVALID_ARGS;
    }

    // The metadata layout of the FVM is dependent on the
    // size of the FVM's underlying partition.
    block_info_t block_info;
    ssize_t rc = ioctl_block_get_info(fd, &block_info);
    if (rc < 0) {
        return static_cast<mx_status_t>(rc);
    } else if (rc != sizeof(block_info)) {
        return MX_ERR_BAD_STATE;
    } else if (slice_size == 0 || slice_size % block_info.block_size) {
        return MX_ERR_BAD_STATE;
    }

    size_t disk_size = block_info.block_count * block_info.block_size;
    size_t metadata_size = fvm::MetadataSize(disk_size, slice_size);

    fbl::unique_ptr<MappedVmo> mvmo;
    mx_status_t status = MappedVmo::Create(metadata_size * 2, "fvm-meta", &mvmo);
    if (status != MX_OK) {
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
        return MX_ERR_NO_SPACE;
    }

    fvm_update_hash(mvmo->GetData(), metadata_size);

    const void* backup = reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(mvmo->GetData()) +
                                                 metadata_size);
    status = fvm_validate_header(mvmo->GetData(), backup, metadata_size, nullptr);
    if (status != MX_OK) {
        return status;
    }

    if (lseek(fd, 0, SEEK_SET) < 0) {
        return MX_ERR_BAD_STATE;
    }
    // Write to primary copy.
    if (write(fd, mvmo->GetData(), metadata_size) != static_cast<ssize_t>(metadata_size)) {
        return MX_ERR_BAD_STATE;
    }
    // Write to secondary copy, to overwrite any previous FVM metadata copy that
    // could be here.
    if (write(fd, mvmo->GetData(), metadata_size) != static_cast<ssize_t>(metadata_size)) {
        return MX_ERR_BAD_STATE;
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

    return MX_OK;
}

// Helper function to allocate, find, and open VPartition.
int fvm_allocate_partition(int fvm_fd, const alloc_req_t* request) {
    ssize_t r;
    if ((r = ioctl_block_fvm_alloc(fvm_fd, request)) != MX_OK) {
        return -1;
    }

    typedef struct {
        const alloc_req_t* request;
        int out_partition;
    } alloc_helper_info_t;

    alloc_helper_info_t info;
    info.request = request;

    auto cb = [](int dirfd, int event, const char* fn, void* cookie) {
        if (event != WATCH_EVENT_ADD_FILE) {
            return MX_OK;
        }
        auto info = static_cast<alloc_helper_info_t*>(cookie);
        int devfd = openat(dirfd, fn, O_RDWR);
        if (devfd < 0) {
            return MX_OK;
        }
        if (is_partition(devfd, info->request->guid, info->request->type)) {
            info->out_partition = devfd;
            return MX_ERR_STOP;
        }
        close(devfd);
        return MX_OK;
    };

    DIR* dir = opendir(kBlockDevPath);
    if (dir == nullptr) {
        return -1;
    }

    mx_time_t deadline = mx_deadline_after(MX_SEC(2));
    if (mxio_watch_directory(dirfd(dir), cb, deadline, &info) != MX_ERR_STOP) {
        return -1;
    }
    closedir(dir);
    return info.out_partition;
}

int fvm_open_partition(const uint8_t* uniqueGUID, const uint8_t* typeGUID, char* out) {
    DIR* dir = opendir(kBlockDevPath);
    if (dir == nullptr) {
        return -1;
    }
    struct dirent* de;
    int result_fd = -1;
    while ((de = readdir(dir)) != NULL) {
        if ((strcmp(de->d_name, ".") == 0) || strcmp(de->d_name, "..") == 0) {
            continue;
        }
        int devfd = openat(dirfd(dir), de->d_name, O_RDWR);
        if (devfd < 0) {
            continue;
        } else if (!is_partition(devfd, uniqueGUID, typeGUID)) {
            close(devfd);
            continue;
        }
        result_fd = devfd;
        if (out != nullptr) {
            strcpy(out, kBlockDevPath);
            strcat(out, de->d_name);
        }
        break;
    }

    closedir(dir);
    return result_fd;
}
