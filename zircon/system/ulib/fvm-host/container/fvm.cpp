// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <errno.h>
#include <inttypes.h>
#include <utility>

#include <fvm/format.h>
#include <lib/fit/defer.h>
#include <sys/ioctl.h>

#include "fvm-host/container.h"

#if defined(__APPLE__)
#include <sys/disk.h>
#define IOCTL_GET_BLOCK_COUNT DKIOCGETBLOCKCOUNT
#endif

#if defined(__linux__)
#include <linux/fs.h>

#define IOCTL_GET_BLOCK_COUNT BLKGETSIZE
#endif

zx_status_t FvmContainer::Create(const char* path, size_t slice_size, off_t offset, off_t length,
                                 fbl::unique_ptr<FvmContainer>* out) {
    fbl::unique_ptr<FvmContainer> fvmContainer(new FvmContainer(path, slice_size, offset, length));

    zx_status_t status;
    if ((status = fvmContainer->Init()) != ZX_OK) {
        return status;
    }

    *out = std::move(fvmContainer);
    return ZX_OK;
}

FvmContainer::FvmContainer(const char* path, size_t slice_size, off_t offset, off_t length)
    : Container(path, slice_size, 0), disk_offset_(offset), disk_size_(length) {
    fd_.reset(open(path, O_RDWR, 0644));
    if (!fd_) {
        if (errno == ENOENT) {
            fd_.reset(open(path, O_RDWR | O_CREAT | O_EXCL, 0644));

            if (!fd_) {
                fprintf(stderr, "Failed to create path %s\n", path);
                exit(-1);
            }

            xprintf("Created path %s\n", path);
        } else {
            fprintf(stderr, "Failed to open path %s: %s\n", path, strerror(errno));
            exit(-1);
        }
    }

    struct stat s;
    if (fstat(fd_.get(), &s) < 0) {
        fprintf(stderr, "Failed to stat %s\n", path);
        exit(-1);
    }

    uint64_t size = s.st_size;

    if (S_ISBLK(s.st_mode)) {
        uint64_t block_count;
        if (ioctl(fd_.get(), IOCTL_GET_BLOCK_COUNT, &block_count) >= 0) {
            size = block_count * 512;
        }
    }

    if (disk_size_ == 0) {
        disk_size_ = size;
    }

    if (size < disk_offset_ + disk_size_) {
        fprintf(stderr, "Invalid file size %" PRIu64 " for specified offset+length\n", size);
        exit(-1);
    }

    // Attempt to load metadata from disk
    fvm::host::FdWrapper wrapper = fvm::host::FdWrapper(fd_.get());
    if (info_.Load(&wrapper, disk_offset_, disk_size_) != ZX_OK) {
        exit(-1);
    }

    if (info_.IsValid()) {
        slice_size_ = info_.SliceSize();
    }
}

FvmContainer::~FvmContainer() = default;

zx_status_t FvmContainer::Init() {
    return info_.Reset(disk_size_, slice_size_);
}

zx_status_t FvmContainer::Verify() const {
    info_.CheckValid();

    zx_status_t status = info_.Validate();
    if (status != ZX_OK) {
        return status;
    }

    fvm::fvm_t* sb = info_.SuperBlock();

    xprintf("Total size is %zu\n", disk_size_);
    xprintf("Metadata size is %zu\n", info_.MetadataSize());
    xprintf("Slice size is %" PRIu64 "\n", info_.SliceSize());
    xprintf("Slice count is %" PRIu64 "\n", info_.SuperBlock()->pslice_count);

    off_t start = 0;
    off_t end = disk_offset_ + info_.MetadataSize() * 2;
    size_t slice_index = 1;
    for (size_t vpart_index = 1; vpart_index < fvm::kMaxVPartitions; ++vpart_index) {
        fvm::vpart_entry_t* vpart = nullptr;
        start = end;

        zx_status_t status;
        if ((status = info_.GetPartition(vpart_index, &vpart)) != ZX_OK) {
            return status;
        }

        if (vpart->slices == 0) {
            break;
        }

        fbl::Vector<size_t> extent_lengths;
        size_t last_vslice = 0;
        size_t slice_count = 0;
        for (; slice_index <= sb->pslice_count; ++slice_index) {
            fvm::SliceEntry* slice = nullptr;
            if ((status = info_.GetSlice(slice_index, &slice)) != ZX_OK) {
                return status;
            }

            if (slice->VPartition() != vpart_index) {
                break;
            }

            end += slice_size_;
            slice_count++;

            if (slice->VSlice() == last_vslice + 1) {
                extent_lengths[extent_lengths.size() - 1] += slice_size_;
            } else {
                extent_lengths.push_back(slice_size_);
            }

            last_vslice = slice->VSlice();
        }

        if (vpart->slices != slice_count) {
            fprintf(stderr, "Reported partition slices do not match expected\n");
            return ZX_ERR_BAD_STATE;
        }

        disk_format_t part;
        if ((status = Format::Detect(fd_.get(), start, &part)) != ZX_OK) {
            return status;
        }

        fbl::unique_fd dupfd(dup(fd_.get()));
        if (!dupfd) {
            fprintf(stderr, "Failed to duplicate fd\n");
            return ZX_ERR_INTERNAL;
        }

        if ((status = Format::Check(std::move(dupfd), start, end, extent_lengths, part)) != ZX_OK) {
            fprintf(stderr, "%s fsck returned an error.\n", vpart->name);
            return status;
        }

        xprintf("Found valid %s partition\n", vpart->name);
    }

    return ZX_OK;
}

zx_status_t FvmContainer::Extend(size_t disk_size) {
    if (disk_size <= disk_size_) {
        fprintf(stderr, "Cannot extend to disk size %zu smaller than current size %" PRIu64 "\n",
                disk_size, disk_size_);
        return ZX_ERR_INVALID_ARGS;
    } else if (disk_offset_) {
        fprintf(stderr, "Cannot extend FVM within another container\n");
        return ZX_ERR_BAD_STATE;
    }

    const char* temp = ".tmp";

    if (path_.length() >= PATH_MAX - strlen(temp) - 1) {
        fprintf(stderr, "Path name exceeds maximum length\n");
        return ZX_ERR_INVALID_ARGS;
    }

    fbl::StringBuffer<PATH_MAX> path;
    path.AppendPrintf("%s%s", path_.c_str(), temp);
    fbl::unique_fd fd(open(path.c_str(), O_RDWR | O_CREAT, 0644));

    if (!fd) {
        fprintf(stderr, "Unable to open temp file %s\n", path.c_str());
        return ZX_ERR_IO;
    }

    auto cleanup = fit::defer([path]() {
        if (unlink(path.c_str()) < 0) {
            fprintf(stderr, "Failed to unlink path %s\n", path.c_str());
        }
    });

    if (ftruncate(fd.get(), disk_size) != 0) {
        fprintf(stderr, "Failed to truncate fvm container");
        return ZX_ERR_IO;
    }

    // Since the size and location of both metadata in an FVM is dependent on the size of
    // the FVM partition, we must relocate any data that already exists within the volume
    // manager.
    //
    // First, we read all old slices from the original device, and write them to their
    // new locations.
    //
    // Then, we update the on-disk metadata to reflect the new size of the disk.
    // To avoid collision between relocated slices, this is done on a temporary file.
    uint64_t pslice_count = info_.SuperBlock()->pslice_count;
    fvm::FormatInfo source_format_info = fvm::FormatInfo::FromDiskSize(disk_size_, slice_size_);
    fvm::FormatInfo target_format_info = fvm::FormatInfo::FromDiskSize(disk_size, slice_size_);
    for (uint32_t index = 1; index <= pslice_count; index++) {
        zx_status_t status;
        fvm::slice_entry_t* slice = nullptr;
        if ((status = info_.GetSlice(index, &slice)) != ZX_OK) {
            fprintf(stderr, "Failed to retrieve slice %u\n", index);
            return status;
        }

        if (slice->IsFree()) {
            continue;
        }

        fbl::Array<uint8_t> data(new uint8_t[slice_size_], slice_size_);

        if (lseek(fd_.get(), source_format_info.GetSliceStart(index), SEEK_SET) < 0) {
            fprintf(stderr, "Cannot seek to slice %u in current FVM\n", index);
            return ZX_ERR_BAD_STATE;
        }

        ssize_t r = read(fd_.get(), data.get(), slice_size_);
        if (r < 0 || static_cast<size_t>(r) != slice_size_) {
            fprintf(stderr, "Failed to read data from FVM: %ld\n", r);
            return ZX_ERR_BAD_STATE;
        }

        if (lseek(fd.get(), target_format_info.GetSliceStart(index), SEEK_SET) < 0) {
            fprintf(stderr, "Cannot seek to slice %u in new FVM\n", index);
            return ZX_ERR_BAD_STATE;
        }

        r = write(fd.get(), data.get(), slice_size_);
        if (r < 0 || static_cast<size_t>(r) != slice_size_) {
            fprintf(stderr, "Failed to write data to FVM: %ld\n", r);
            return ZX_ERR_BAD_STATE;
        }
    }

    size_t metadata_size = fvm::MetadataSize(disk_size, slice_size_);
    zx_status_t status = info_.Grow(metadata_size);
    if (status != ZX_OK) {
        return status;
    }

    fvm::host::FdWrapper wrapper = fvm::host::FdWrapper(fd.get());
    if ((status = info_.Write(&wrapper, 0, disk_size)) != ZX_OK) {
        return status;
    }

    fd_.reset(fd.release());
    disk_size_ = disk_size;

    if ((status = Verify()) != ZX_OK) {
        fprintf(stderr, "Verify failed - cancelling extension\n");
        return status;
    }

    if (rename(path.c_str(), path_.c_str()) < 0) {
        fprintf(stderr, "Failed to copy over temp file\n");
        return ZX_ERR_IO;
    }

    cleanup.cancel();
    return ZX_OK;
}

zx_status_t FvmContainer::Commit() {
    if (!info_.IsDirty()) {
        fprintf(stderr, "Commit: Nothing to write\n");
        return ZX_OK;
    }

    // If the FVM container has just been created, truncate it to an appropriate size
    if (disk_size_ == 0) {
        if (partitions_.is_empty()) {
            fprintf(stderr, "Cannot create new FVM container with 0 partitions\n");
            return ZX_ERR_INVALID_ARGS;
        }

        uint64_t total_size = CalculateDiskSize();
        zx_status_t status = info_.Grow(fvm::MetadataSize(total_size, slice_size_));
        if (status != ZX_OK) {
            return status;
        }

        if (ftruncate(fd_.get(), total_size) != 0) {
            fprintf(stderr, "Failed to truncate fvm container");
            return ZX_ERR_IO;
        }

        struct stat s;
        if (fstat(fd_.get(), &s) < 0) {
            fprintf(stderr, "Failed to stat container\n");
            return ZX_ERR_IO;
        }

        disk_size_ = s.st_size;

        if (disk_size_ != total_size) {
            fprintf(stderr, "Truncated to incorrect size\n");
            return ZX_ERR_IO;
        }
    }

    fvm::host::FdWrapper wrapper = fvm::host::FdWrapper(fd_.get());
    zx_status_t status = info_.Write(&wrapper, disk_offset_, disk_size_);
    if (status != ZX_OK) {
        return status;
    }

    for (unsigned i = 0; i < partitions_.size(); i++) {
        if ((status = WritePartition(i)) != ZX_OK) {
            return status;
        }
    }

    xprintf("Successfully wrote FVM data to disk\n");
    return ZX_OK;
}

size_t FvmContainer::SliceSize() const {
    info_.CheckValid();
    return slice_size_;
}

zx_status_t FvmContainer::AddPartition(const char* path, const char* type_name,
                                       FvmReservation* reserve) {
    info_.CheckValid();
    fbl::unique_ptr<Format> format;
    zx_status_t status;
    if ((status = Format::Create(path, type_name, &format)) != ZX_OK) {
        fprintf(stderr, "Failed to initialize partition\n");
        return status;
    }

    uint32_t vpart_index;
    uint8_t guid[fvm::kGuidSize];
    format->Guid(guid);
    fvm::partition_descriptor_t descriptor;
    format->GetPartitionInfo(&descriptor);
    if ((status = info_.AllocatePartition(&descriptor, guid, &vpart_index)) != ZX_OK) {
        return status;
    }

    if ((status = format->MakeFvmReady(slice_size_, vpart_index, reserve)) != ZX_OK) {
        return status;
    }

    uint32_t slice_count = 0;
    if ((status = format->GetSliceCount(&slice_count)) != ZX_OK) {
        return status;
    }

    // If allocated metadata is too small, grow it to an appropriate size
    if ((status = info_.GrowForSlices(slice_count)) != ZX_OK) {
        return status;
    }

    // Allocate all slices for this partition
    uint32_t pslice_start = 0;
    uint32_t pslice_total = 0;
    unsigned extent_index = 0;
    while (true) {
        vslice_info_t vslice_info;
        zx_status_t status;
        if ((status = format->GetVsliceRange(extent_index, &vslice_info)) != ZX_OK) {
            if (status == ZX_ERR_OUT_OF_RANGE) {
                break;
            }
            return status;
        }

        uint32_t vslice =
            static_cast<uint32_t>(vslice_info.vslice_start / format->BlocksPerSlice());

        for (unsigned i = 0; i < vslice_info.slice_count; i++) {
            uint32_t pslice;

            if ((status = info_.AllocateSlice(format->VpartIndex(), vslice + i, &pslice)) !=
                ZX_OK) {
                return status;
            }

            if (!pslice_start) {
                pslice_start = pslice;
            }

            // On a new FVM container, pslice allocation is expected to be contiguous.
            if (pslice != pslice_start + pslice_total) {
                fprintf(stderr, "Unexpected error during slice allocation\n");
                return ZX_ERR_INTERNAL;
            }

            pslice_total++;
        }

        extent_index++;
    }

    fvm::vpart_entry_t* entry;
    if ((status = info_.GetPartition(format->VpartIndex(), &entry)) != ZX_OK) {
        return status;
    }

    ZX_ASSERT(entry->slices == slice_count);

    FvmPartitionInfo partition;
    partition.format = std::move(format);
    partition.vpart_index = vpart_index;
    partition.pslice_start = pslice_start;
    partition.slice_count = slice_count;
    partitions_.push_back(std::move(partition));
    return ZX_OK;
}

uint64_t FvmContainer::CalculateDiskSize() const {
    info_.CheckValid();

    size_t required_slices = 0;

    for (size_t index = 1; index < fvm::kMaxVPartitions; index++) {
        fvm::vpart_entry_t* vpart;
        ZX_ASSERT(info_.GetPartition(index, &vpart) == ZX_OK);

        if (vpart->slices == 0) {
            break;
        }

        required_slices += vpart->slices;
    }

    return CalculateDiskSizeForSlices(required_slices);
}

uint64_t FvmContainer::GetDiskSize() const {
    return disk_size_;
}

zx_status_t FvmContainer::WritePartition(unsigned part_index) {
    info_.CheckValid();
    if (part_index > partitions_.size()) {
        fprintf(stderr, "Error: Tried to access partition %u / %zu\n", part_index,
                partitions_.size());
        return ZX_ERR_OUT_OF_RANGE;
    }

    unsigned extent_index = 0;
    FvmPartitionInfo* partition = &partitions_[part_index];
    Format* format = partition->format.get();
    uint32_t pslice_start = partition->pslice_start;

    while (true) {
        zx_status_t status;
        if ((status = WriteExtent(extent_index++, format, &pslice_start)) != ZX_OK) {
            if (status != ZX_ERR_OUT_OF_RANGE) {
                return status;
            }

            return ZX_OK;
        }
    }
}

zx_status_t FvmContainer::WriteExtent(unsigned extent_index, Format* format, uint32_t* pslice) {
    vslice_info_t vslice_info{};
    zx_status_t status;
    if ((status = format->GetVsliceRange(extent_index, &vslice_info)) != ZX_OK) {
        return status;
    }

    // Write each slice in the given extent
    uint32_t current_block = 0;
    for (unsigned i = 0; i < vslice_info.slice_count; i++) {
        // Write each block in this slice
        for (uint32_t j = 0; j < format->BlocksPerSlice(); j++) {
            // If we have gone beyond the blocks written to partition file, write empty block
            if (current_block >= vslice_info.block_count) {
                if (!vslice_info.zero_fill) {
                    break;
                }
                format->EmptyBlock();
            } else {
                if ((status = format->FillBlock(vslice_info.block_offset + current_block)) !=
                    ZX_OK) {
                    fprintf(stderr, "Failed to read block from minfs\n");
                    return status;
                }

                current_block++;
            }

            if ((status = WriteData(*pslice, j, format->BlockSize(), format->Data())) != ZX_OK) {
                fprintf(stderr, "Failed to write data to FVM\n");
                return status;
            }
        }
        (*pslice)++;
    }

    return ZX_OK;
}

zx_status_t FvmContainer::WriteData(uint32_t pslice, uint32_t block_offset, size_t block_size,
                                    void* data) {
    info_.CheckValid();
    fvm::FormatInfo format_info = fvm::FormatInfo::FromDiskSize(disk_size_, slice_size_);
    if (block_offset * block_size > slice_size_) {
        fprintf(stderr, "Not enough space in slice\n");
        return ZX_ERR_OUT_OF_RANGE;
    }

    if (lseek(fd_.get(),
              disk_offset_ + format_info.GetSliceStart(pslice) + block_offset * block_size,
              SEEK_SET) < 0) {
        return ZX_ERR_BAD_STATE;
    }

    ssize_t r = write(fd_.get(), data, block_size);
    if (r < 0 || static_cast<size_t>(r) != block_size) {
        fprintf(stderr, "Failed to write data to FVM\n");
        return ZX_ERR_BAD_STATE;
    }

    return ZX_OK;
}
