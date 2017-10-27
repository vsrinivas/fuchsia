// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "container.h"

zx_status_t FvmContainer::Create(const char* path, size_t slice_size,
                                 fbl::unique_ptr<FvmContainer>* out) {
    fbl::AllocChecker ac;
    fbl::unique_ptr<FvmContainer> fvmContainer(new (&ac) FvmContainer(path, slice_size));
    if (!ac.check()) {
        return ZX_ERR_NO_MEMORY;
    }

    zx_status_t status;
    if ((status = fvmContainer->Init()) != ZX_OK) {
        return status;
    }

    *out = fbl::move(fvmContainer);
    return ZX_OK;
}

FvmContainer::FvmContainer(const char* path, size_t slice_size)
    : Container(slice_size), valid_(false),
      vpart_hint_(1), pslice_hint_(1) {
    fd_.reset(open(path, O_RDWR, 0644));
    if (!fd_) {
        printf("Failed to open path %s\n", path);
        exit(-1);
    }

    struct stat s;
    if (fstat(fd_.get(), &s) < 0) {
        printf("Failed to stat %s\n", path);
        exit(-1);
    }

    disk_size_ = s.st_size;
    block_size_ = s.st_blksize;
    block_count_ = s.st_blocks;
    metadata_size_ = fvm::MetadataSize(disk_size_, slice_size);

    fbl::AllocChecker ac;
    metadata_.reset(new (&ac) uint8_t[metadata_size_ * 2]);
    if (!ac.check()) {
        printf("Unable to acquire resources for metadata\n");
        exit(-1);
    }

    if (lseek(fd_.get(), 0, SEEK_SET) < 0) {
        printf("Seek reset failed\n");
        exit(-1);
    }

    // Clear entire primary copy of metadata
    memset(metadata_.get(), 0, metadata_size_);

    ssize_t rd = read(fd_.get(), metadata_.get(), metadata_size_ * 2);
    if (rd != static_cast<ssize_t>(metadata_size_ * 2)) {
        printf("Metadata read failed: expected %ld, actual %ld\n", metadata_size_, rd);
        exit(-1);
    }

    const void* backup = reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(metadata_.get()) +
                                                 metadata_size_);

    // For now we always assume that primary metadata is primary
    if (fvm_validate_header(metadata_.get(), backup, metadata_size_, nullptr) == ZX_OK) {
        valid_ = true;

        if (memcmp(metadata_.get(), backup, metadata_size_)) {
            printf("Warning: primary and backup metadata do not match\n");
        }
    }
}

FvmContainer::~FvmContainer() = default;

zx_status_t FvmContainer::Init() {
    // Superblock
    fvm::fvm_t* sb = static_cast<fvm::fvm_t*>((void*)metadata_.get());
    sb->magic = FVM_MAGIC;
    sb->version = FVM_VERSION;
    sb->pslice_count = (disk_size_ - metadata_size_ * 2) / slice_size_;
    sb->slice_size = slice_size_;
    sb->fvm_partition_size = disk_size_;
    sb->vpartition_table_size = fvm::kVPartTableLength;
    sb->allocation_table_size = fvm::AllocTableLength(disk_size_, slice_size_);
    sb->generation = 0;

    if (sb->pslice_count == 0) {
        return ZX_ERR_NO_SPACE;
    }

    dirty_ = true;
    valid_ = true;

    printf("fvm_init: Success\n");
    printf("fvm_init: Slice Count: %" PRIu64 ", size: %" PRIu64 "\n", sb->pslice_count, sb->slice_size);
    printf("fvm_init: Vpart offset: %zu, length: %zu\n",
           fvm::kVPartTableOffset, fvm::kVPartTableLength);
    printf("fvm_init: Atable offset: %zu, length: %zu\n",
           fvm::kAllocTableOffset, fvm::AllocTableLength(disk_size_, slice_size_));
    printf("fvm_init: Backup meta starts at: %zu\n",
           fvm::BackupStart(disk_size_, slice_size_));
    printf("fvm_init: Slices start at %zu, there are %zu of them\n",
           fvm::SlicesStart(disk_size_, slice_size_),
           fvm::UsableSlicesCount(disk_size_, slice_size_));
    return ZX_OK;
}

zx_status_t FvmContainer::Verify() const {
    CheckValid();
    const void* backup = reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(metadata_.get()) +
                                                 metadata_size_);

    if (fvm_validate_header(metadata_.get(), backup, metadata_size_, nullptr) != ZX_OK) {
        printf("Failed to validate header\n");
        return ZX_ERR_BAD_STATE;
    }

    fvm::fvm_t* sb = static_cast<fvm::fvm_t*>((void*)metadata_.get());

    printf("Total size is %zu\n", disk_size_);
    printf("Metadata size is %zu\n", metadata_size_);
    printf("Slice size is %" PRIu64 "\n", sb->slice_size);
    printf("Slice count is %" PRIu64 "\n", sb->pslice_count);

    uintptr_t metadata_start = reinterpret_cast<uintptr_t>(metadata_.get());
    uintptr_t offset = static_cast<uintptr_t>(fvm::kVPartTableOffset +
                                              1 * sizeof(fvm::vpart_entry_t));

    fvm::vpart_entry_t* entry = reinterpret_cast<fvm::vpart_entry_t*>(metadata_start + offset);

    //TODO(planders): Report all partitions found
    printf("Just created entry with slice count %u, name %s\n", entry->slices, entry->name);
    return ZX_OK;
}

zx_status_t FvmContainer::Commit() {
    if (!dirty_) {
        printf("Commit: Nothing to write\n");
        return ZX_OK;
    }

    fvm_update_hash(metadata_.get(), metadata_size_);

    if (lseek(fd_.get(), 0, SEEK_SET) < 0) {
        printf("Error seeking disk\n");
        return ZX_ERR_IO;
    }

    if (write(fd_.get(), metadata_.get(), metadata_size_) != static_cast<ssize_t>(metadata_size_)) {
        printf("Error writing metadata to disk\n");
        return ZX_ERR_IO;
    }

    if (write(fd_.get(), metadata_.get(), metadata_size_) != static_cast<ssize_t>(metadata_size_)) {
        printf("Error writing metadata to disk\n");
        return ZX_ERR_IO;
    }

    printf("Successfully wrote metadata to disk\n");
    return ZX_OK;
}

size_t FvmContainer::SliceSize() const {
    CheckValid();
    return slice_size_;
}

zx_status_t FvmContainer::AddPartition(const char* path, const char* type_name) {
    fbl::unique_ptr<Format> format;
    zx_status_t status;
    if ((status = Format::Create(path, type_name, &format)) != ZX_OK) {
        printf("Failed to initialize partition\n");
        return status;
    }

    uint8_t guid[FVM_GUID_LEN];
    uint8_t type[FVM_GUID_LEN];
    char name[FVM_NAME_LEN];
    format->Guid(guid);
    format->Type(type);
    format->Name(name);
    uint32_t vpart_index;
    if ((status = AllocatePartition(type, guid, name, 1, &vpart_index)) != ZX_OK) {
        return status;
    }

    if ((status = format->MakeFvmReady(SliceSize(), vpart_index)) != ZX_OK) {
        fprintf(stderr, "Failed to MakeFvmReady minfs partition\n");
        return status;
    }

    unsigned i = 0;
    while (true) {
        if ((status = WriteExtent(i++, format.get())) != ZX_OK) {
            if (status != ZX_ERR_OUT_OF_RANGE) {
                return status;
            }

            return ZX_OK;
        }
    }
}

void FvmContainer::CheckValid() const {
    if (!valid_) {
        fprintf(stderr, "Error: FVM is invalid\n");
        exit(-1);
    }
}

zx_status_t FvmContainer::AllocatePartition(uint8_t* type, uint8_t* guid, const char* name,
                                            uint32_t slices, uint32_t* vpart_index) {
    CheckValid();
    for (unsigned index = vpart_hint_; index < FVM_MAX_ENTRIES; index++) {
        uintptr_t metadata_start = reinterpret_cast<uintptr_t>(metadata_.get());
        uintptr_t offset = static_cast<uintptr_t>(fvm::kVPartTableOffset +
                                                  index * sizeof(fvm::vpart_entry_t));

        fvm::vpart_entry_t* entry = reinterpret_cast<fvm::vpart_entry_t*>(metadata_start +
                                                                          offset);

        // Make sure this vpartition has not already been allocated
        if (entry->slices == 0) {
            entry->init(type, guid, slices, name, 0);
            vpart_hint_ = index + 1;
            dirty_ = true;
            *vpart_index = index;
            return ZX_OK;
        }
    }

    return ZX_ERR_INTERNAL;
}

zx_status_t FvmContainer::AllocateSlice(uint32_t vpart, uint32_t vslice, uint32_t* pslice) {
    CheckValid();
    fvm::fvm_t* sb = static_cast<fvm::fvm_t*>((void*)metadata_.get());

    for (uint32_t index = pslice_hint_; index < sb->pslice_count; index++) {
        uintptr_t metadata_start = reinterpret_cast<uintptr_t>(metadata_.get());
        uintptr_t offset = static_cast<uintptr_t>(fvm::kAllocTableOffset +
                                                  index * sizeof(fvm::slice_entry_t));
        fvm::slice_entry_t* entry = reinterpret_cast<fvm::slice_entry_t*>(metadata_start +
                                                                          offset);

        if (entry->vpart != FVM_SLICE_FREE) {
            continue;
        }
        pslice_hint_ = index + 1;

        entry->vpart = vpart & VPART_MAX;
        entry->vslice = vslice & VSLICE_MAX;
        dirty_ = true;
        *pslice = index;
        return ZX_OK;
    }

    return ZX_ERR_INTERNAL;
}

zx_status_t FvmContainer::WriteExtent(unsigned vslice_count, Format* format) {
    vslice_info_t vslice_info;
    zx_status_t status;
    if ((status = format->GetVsliceRange(vslice_count, &vslice_info)) != ZX_OK) {
        return status;
    }

    uint32_t current_block = 0;
    uint32_t vslice = vslice_info.vslice_start / format->BlocksPerSlice();

    for (unsigned i = 0; i < vslice_info.slice_count; i++) {
        uint32_t pslice;

        if ((status = AllocateSlice(format->VpartIndex(), vslice + i, &pslice)) != ZX_OK) {
            return status;
        }

        for (uint32_t j = 0; j < format->BlocksPerSlice(); j++) {
            // If we have gone beyond the blocks written to partition file, write empty block
            if (current_block >= vslice_info.block_count) {
                if (!vslice_info.zero_fill) {
                    break;
                }

                format->EmptyBlock();
            } else {
                if ((status = format->FillBlock(vslice_info.block_offset + current_block)) != ZX_OK) {
                    fprintf(stderr, "Failed to read block from minfs\n");
                    return status;
                }

                current_block++;
            }

            if ((status = WriteData(format->VpartIndex(), pslice, format->Data(), j,
                                    format->BlockSize())) != ZX_OK) {
                fprintf(stderr, "Failed to write data to FVM\n");
                return status;
            }
        }
    }

    return ZX_OK;
}

zx_status_t FvmContainer::WriteData(uint32_t vpart, uint32_t pslice, void* data, uint32_t block_offset,
                                    size_t block_size) {
    CheckValid();

    if (block_offset * block_size > slice_size_) {
        printf("Not enough space in slice\n");
        return ZX_ERR_OUT_OF_RANGE;
    }

    if (lseek(fd_.get(), fvm::SliceStart(disk_size_, slice_size_, pslice) +
                block_offset * block_size, SEEK_SET) < 0) {
        return ZX_ERR_BAD_STATE;
    }

    ssize_t r = write(fd_.get(), data, block_size);
    if (r != block_size) {
        printf("Failed to write data to FVM\n");
        return ZX_ERR_BAD_STATE;
    }

    return ZX_OK;
}
