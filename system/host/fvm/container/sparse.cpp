// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <inttypes.h>
#include <utility>

#include "fvm/container.h"

constexpr size_t kLz4HeaderSize = 15;

static LZ4F_preferences_t lz4_prefs = {
    .frameInfo = {
        .blockSizeID = LZ4F_max64KB,
        .blockMode = LZ4F_blockIndependent,
    },
    .compressionLevel = 0,
};

zx_status_t CompressionContext::Setup(size_t max_len) {
    LZ4F_errorCode_t errc = LZ4F_createCompressionContext(&cctx_, LZ4F_VERSION);
    if (LZ4F_isError(errc)) {
        fprintf(stderr, "Could not create compression context: %s\n", LZ4F_getErrorName(errc));
        return ZX_ERR_INTERNAL;
    }

    Reset(kLz4HeaderSize + LZ4F_compressBound(max_len, &lz4_prefs));

    size_t r = LZ4F_compressBegin(cctx_, GetBuffer(), GetRemaining(), &lz4_prefs);
    if (LZ4F_isError(r)) {
        fprintf(stderr, "Could not begin compression: %s\n", LZ4F_getErrorName(r));
        return ZX_ERR_INTERNAL;
    }

    IncreaseOffset(r);
    return ZX_OK;
}

zx_status_t CompressionContext::Compress(const void* data, size_t length) {
    size_t r = LZ4F_compressUpdate(cctx_, GetBuffer(), GetRemaining(), data, length, NULL);
    if (LZ4F_isError(r)) {
        fprintf(stderr, "Could not compress data: %s\n", LZ4F_getErrorName(r));
        return ZX_ERR_INTERNAL;
    }

    IncreaseOffset(r);
    return ZX_OK;
}

zx_status_t CompressionContext::Finish() {
    size_t r = LZ4F_compressEnd(cctx_, GetBuffer(), GetRemaining(), NULL);
    if (LZ4F_isError(r)) {
        fprintf(stderr, "Could not finish compression: %s\n", LZ4F_getErrorName(r));
        return ZX_ERR_INTERNAL;
    }

    IncreaseOffset(r);
    LZ4F_errorCode_t errc = LZ4F_freeCompressionContext(cctx_);
    if (LZ4F_isError(errc)) {
        fprintf(stderr, "Could not free compression context: %s\n", LZ4F_getErrorName(errc));
        return ZX_ERR_INTERNAL;
    }

    return ZX_OK;
}

zx_status_t SparsePaver::Create(const char* path, size_t slice_size, size_t disk_offset,
                                size_t disk_size, fbl::unique_ptr<SparsePaver>* out) {
    fbl::unique_ptr<SparsePaver> paver(new SparsePaver(disk_offset, disk_size));

    zx_status_t status = paver->Init(path, slice_size);

    if (status != ZX_OK) {
        return status;
    }

    *out = std::move(paver);
    return status;
}

zx_status_t SparsePaver::AddPartition(const partition_info_t* partition,
                                      fvm::SparseReader* reader) {
    info_.CheckValid();

    // Assign random guid.
    uint8_t guid[FVM_GUID_LEN];
    static unsigned int seed = time(0);
    for (size_t i = 0; i < FVM_GUID_LEN; i++) {
        guid[i] = rand_r(&seed);
    }

    uint32_t vpart_index;
    const fvm::partition_descriptor_t* descriptor = &partition->descriptor;
    zx_status_t status = info_.AllocatePartition(descriptor, guid, &vpart_index);
    if (status != ZX_OK) {
        return status;
    }

    // Allocate all slices for this partition.
    for (uint32_t i = 0; i < descriptor->extent_count; i++) {
        status = AddExtent(vpart_index, &partition->extents[i], reader);
        if (status != ZX_OK) {
            return status;
        }
    }

    return ZX_OK;
}

zx_status_t SparsePaver::Commit() {
    info_.CheckValid();

    if (disk_ptr_ > disk_offset_ + disk_size_) {
        fprintf(stderr, "FVM metadata size exceeds disk size\n");
        return ZX_ERR_INTERNAL;
    }

    zx_status_t status = info_.Write(fd_, disk_offset_, disk_size_);

    if (status != ZX_OK) {
        return status;
    }

    // Move pointer to the end of the designated partition size to prevent any further edits.
    disk_ptr_ = disk_offset_ + disk_size_ + 1;
    return ZX_OK;
}

zx_status_t SparsePaver::Init(const char* path, size_t slice_size) {
    fd_.reset(open(path, O_WRONLY, 0644));
    if (!fd_) {
        return ZX_ERR_IO;
    }

    zx_status_t status = info_.Reset(disk_size_, slice_size);
    if (status != ZX_OK) {
        return status;
    }

    disk_ptr_ = disk_offset_ + info_.MetadataSize() * 2;
    if (disk_ptr_ >= disk_offset_ + disk_size_) {
        fprintf(stderr, "FVM metadata size exceeds disk size\n");
        return ZX_ERR_INTERNAL;
    }

    if (lseek(fd_.get(), disk_ptr_, SEEK_SET) != disk_ptr_) {
        return ZX_ERR_IO;
    }

    data_.reset(new uint8_t[info_.SliceSize()]);
    return ZX_OK;
}

zx_status_t SparsePaver::AddExtent(uint32_t vpart_index, fvm::extent_descriptor_t* extent,
                                   fvm::SparseReader* reader) {
    uint32_t pslice_start = 0;
    uint32_t pslice_total = 0;

    size_t bytes_left = extent->extent_length;
    uint32_t vslice = extent->slice_start;

    for (unsigned i = 0; i < extent->slice_count; i++) {
        uint32_t pslice;

        zx_status_t status = info_.AllocateSlice(vpart_index, vslice + i, &pslice);
        if (status != ZX_OK) {
            return status;
        }

        if (!pslice_start) {
            pslice_start = pslice;
        }

        // On a new FVM container, pslice allocation is expected to be contiguous.
        if (pslice != pslice_start + pslice_total) {
            fprintf(stderr,
                    "fvm: pslice allocation unexpectedly non-contiguous (internal error)\n");
            return ZX_ERR_INTERNAL;
        }

        if ((status = WriteSlice(&bytes_left, reader)) != ZX_OK) {
            return status;
        }

        pslice_total++;
    }

    return ZX_OK;
}

zx_status_t SparsePaver::WriteSlice(size_t* bytes_left, fvm::SparseReader* reader) {
    info_.CheckValid();
    const size_t slice_size = info_.SliceSize();

    if (disk_ptr_ + slice_size > disk_offset_ + disk_size_) {
        fprintf(stderr, "Partition data exceeds the provided disk size\n");
        return ZX_ERR_INVALID_ARGS;
    }

    size_t read_length = fbl::min(slice_size, *bytes_left);

    if (read_length > 0) {
        size_t bytes_read = 0;
        zx_status_t status = reader->ReadData(data_.get(), read_length, &bytes_read);
        if (status != ZX_OK) {
            return status;
        }

        if (bytes_read < read_length) {
            fprintf(stderr, "Slice data is less than expected\n");
            return ZX_ERR_IO;
        }

        if (read_length < slice_size) {
            memset(&data_[read_length], 0, slice_size - read_length);
        }

        *bytes_left -= bytes_read;
    } else {
        memset(data_.get(), 0, slice_size);
    }

    ssize_t result = write(fd_.get(), data_.get(), slice_size);
    if (result != slice_size) {
        return ZX_ERR_IO;
    }

    disk_ptr_ += slice_size;
    return ZX_OK;
}

zx_status_t SparseContainer::Create(const char* path, size_t slice_size, uint32_t flags,
                                    fbl::unique_ptr<SparseContainer>* out) {
    fbl::AllocChecker ac;
    fbl::unique_ptr<SparseContainer> sparseContainer(new (&ac) SparseContainer(path, slice_size,
                                                                               flags));
    if (!ac.check()) {
        return ZX_ERR_NO_MEMORY;
    }

    zx_status_t status;
    if ((status = sparseContainer->Init()) != ZX_OK) {
        return status;
    }

    *out = std::move(sparseContainer);
    return ZX_OK;
}

SparseContainer::SparseContainer(const char* path, uint64_t slice_size, uint32_t flags)
    : Container(path, slice_size, flags), valid_(false), disk_size_(0),
      extent_size_(0) {
    fd_.reset(open(path, O_CREAT | O_RDWR, 0666));

    if (!fd_) {
        fprintf(stderr, "Failed to open sparse data path\n");
        return;
    }

    struct stat s;
    if (fstat(fd_.get(), &s) < 0) {
        fprintf(stderr, "Failed to stat %s\n", path);
        return;
    }

    if (s.st_size > 0) {
        disk_size_ = s.st_size;

        fbl::unique_fd dup_fd(dup(fd_.get()));
        if (fvm::SparseReader::CreateSilent(std::move(dup_fd), &reader_) != ZX_OK) {
            fprintf(stderr, "SparseContainer: Failed to read metadata from sparse file\n");
            return;
        }

        memcpy(&image_, reader_->Image(), sizeof(fvm::sparse_image_t));
        slice_size_ = image_.slice_size;
        extent_size_ = disk_size_ - image_.header_length;

        uintptr_t partition_ptr = reinterpret_cast<uintptr_t>(reader_->Partitions());

        for (unsigned i = 0; i < image_.partition_count; i++) {
            partition_info_t partition;
            memcpy(&partition.descriptor, reinterpret_cast<void*>(partition_ptr),
                   sizeof(fvm::partition_descriptor_t));
            partitions_.push_back(std::move(partition));
            partition_ptr += sizeof(fvm::partition_descriptor_t);

            for (size_t j = 0; j < partitions_[i].descriptor.extent_count; j++) {
                fvm::extent_descriptor_t extent;
                memcpy(&extent, reinterpret_cast<void*>(partition_ptr),
                       sizeof(fvm::extent_descriptor_t));
                partitions_[i].extents.push_back(extent);
                partition_ptr += sizeof(fvm::extent_descriptor_t);
            }
        }

        valid_ = true;
        xprintf("Successfully read from existing sparse data container.\n");
    }
}

SparseContainer::~SparseContainer() = default;

zx_status_t SparseContainer::Init() {
    if (slice_size_ == 0) {
        fprintf(stderr, "Cannot initialize sparse container with no slice size\n");
        return ZX_ERR_BAD_STATE;
    }

    image_.magic = fvm::kSparseFormatMagic;
    image_.version = fvm::kSparseFormatVersion;
    image_.slice_size = slice_size_;
    image_.partition_count = 0;
    image_.header_length = sizeof(fvm::sparse_image_t);
    image_.flags = flags_;
    partitions_.reset();
    dirty_ = true;
    valid_ = true;
    extent_size_ = 0;
    xprintf("Initialized new sparse data container.\n");
    return ZX_OK;
}

zx_status_t SparseContainer::Verify() const {
    CheckValid();

    if (image_.flags & fvm::kSparseFlagLz4) {
        // Decompression must occur before verification, since all contents must be available for
        // fsck.
        fprintf(stderr, "SparseContainer: Found compressed container; contents cannot be"
                " verified\n");
        return ZX_ERR_INVALID_ARGS;
    }

    if (image_.magic != fvm::kSparseFormatMagic) {
        fprintf(stderr, "SparseContainer: Bad magic\n");
        return ZX_ERR_IO;
    }

    xprintf("Slice size is %" PRIu64 "\n", image_.slice_size);
    xprintf("Found %" PRIu64 " partitions\n", image_.partition_count);

    off_t start = 0;
    off_t end = image_.header_length;
    for (unsigned i = 0; i < image_.partition_count; i++) {
        fbl::Vector<size_t> extent_lengths;
        start = end;
        xprintf("Found partition %u with %u extents\n", i,
               partitions_[i].descriptor.extent_count);

        for (unsigned j = 0; j < partitions_[i].descriptor.extent_count; j++) {
            extent_lengths.push_back(partitions_[i].extents[j].extent_length);
            end += partitions_[i].extents[j].extent_length;
        }

        zx_status_t status;
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
            const char* name = reinterpret_cast<const char*>(partitions_[i].descriptor.name);
            fprintf(stderr, "%s fsck returned an error.\n", name);
            return status;
        }
    }

    if (end != disk_size_) {
        fprintf(stderr, "Header + extent sizes (%" PRIu64 ") do not match sparse file size "
                "(%zu)\n", end, disk_size_);
        return ZX_ERR_IO_DATA_INTEGRITY;
    }

    return ZX_OK;
}

zx_status_t SparseContainer::CheckDiskSize(uint64_t target_disk_size) const {
    CheckValid();

    size_t usable_slices = fvm::UsableSlicesCount(target_disk_size, image_.slice_size);
    size_t required_slices = SliceCount();

    if (usable_slices < required_slices) {
        return ZX_ERR_OUT_OF_RANGE;
    }

    uint64_t required_disk_size = fvm::SlicesStart(target_disk_size, image_.slice_size)
                                + (required_slices * image_.slice_size);
    if (target_disk_size < required_disk_size) {
        return ZX_ERR_OUT_OF_RANGE;
    }

    return ZX_OK;
}

uint64_t SparseContainer::CalculateDiskSize() const {
    CheckValid();
    return CalculateDiskSizeForSlices(SliceCount());
}

zx_status_t SparseContainer::Commit() {
    if (!dirty_ || image_.partition_count == 0) {
        fprintf(stderr, "Commit: Nothing to write.\n");
        return ZX_OK;
    }

    // Reset file length to 0
    if (ftruncate(fd_.get(), 0) != 0) {
        fprintf(stderr, "Failed to truncate fvm container");
        return ZX_ERR_IO;
    }

    // Recalculate and verify header length
    uint64_t header_length = 0;

    if (lseek(fd_.get(), 0, SEEK_SET) < 0) {
        fprintf(stderr, "Seek reset failed\n");
        return ZX_ERR_IO;
    }

    header_length += sizeof(fvm::sparse_image_t);
    if (write(fd_.get(), &image_, sizeof(fvm::sparse_image_t)) != sizeof(fvm::sparse_image_t)) {
        fprintf(stderr, "Write sparse image header failed\n");
        return ZX_ERR_IO;
    }

    for (unsigned i = 0; i < image_.partition_count; i++) {
        fvm::partition_descriptor_t partition = partitions_[i].descriptor;

        header_length += sizeof(fvm::partition_descriptor_t);
        if (write(fd_.get(), &partition, sizeof(fvm::partition_descriptor_t))
            != sizeof(fvm::partition_descriptor_t)) {
            fprintf(stderr, "Write partition failed\n");
            return ZX_ERR_IO;
        }

        for (unsigned j = 0; j < partition.extent_count; j++) {
            fvm::extent_descriptor_t extent = partitions_[i].extents[j];
            header_length += sizeof(fvm::extent_descriptor_t);
            if (write(fd_.get(), &extent, sizeof(fvm::extent_descriptor_t))
                != sizeof(fvm::extent_descriptor_t)) {
                fprintf(stderr, "Write extent failed\n");
                return ZX_ERR_IO;
            }
        }
    }

    if (header_length != image_.header_length) {
        fprintf(stderr, "Header length does not match!\n");
        return ZX_ERR_INTERNAL;
    }

    zx_status_t status;
    if ((status = PrepareWrite(extent_size_)) != ZX_OK) {
        return status;
    }

    // Write each partition out to sparse file
    for (unsigned i = 0; i < image_.partition_count; i++) {
        fvm::partition_descriptor_t partition = partitions_[i].descriptor;
        Format* format = partitions_[i].format.get();

        vslice_info_t vslice_info;
        // Write out each extent in the partition
        for (unsigned j = 0; j < partition.extent_count; j++) {
            if (format->GetVsliceRange(j, &vslice_info) != ZX_OK) {
                fprintf(stderr, "Unable to access partition extent\n");
                return ZX_ERR_OUT_OF_RANGE;
            }

            // Write out each block in the extent
            for (unsigned k = 0; k < vslice_info.block_count; k++) {
                if (format->FillBlock(vslice_info.block_offset + k) != ZX_OK) {
                    fprintf(stderr, "Failed to read block\n");
                    return ZX_ERR_IO;
                }

                if (WriteData(format->Data(), format->BlockSize()) != ZX_OK) {
                    fprintf(stderr, "Failed to write data to sparse file\n");
                    return ZX_ERR_IO;
                }
            }
        }
    }

    if ((status = CompleteWrite()) != ZX_OK) {
        return status;
    }

    struct stat s;
    if (fstat(fd_.get(), &s) < 0) {
        fprintf(stderr, "Failed to stat container\n");
        return ZX_ERR_IO;
    }

    disk_size_ = s.st_size;
    xprintf("Successfully wrote sparse data to disk.\n");
    return ZX_OK;
}

zx_status_t SparseContainer::Pave(const char* path, size_t disk_offset, size_t disk_size) {
    if (disk_size == 0) {
        // If target disk does not already exist, create it.
        if (disk_offset > 0) {
            fprintf(stderr, "Cannot specify offset without length\n");
            return ZX_ERR_INVALID_ARGS;
        }

        fbl::unique_fd fd(open(path, O_CREAT | O_EXCL | O_WRONLY, 0644));

        if (!fd) {
            return ZX_ERR_IO;
        }

        disk_size = CalculateDiskSize();

        if (ftruncate(fd.get(), disk_size) < 0) {
            return ZX_ERR_IO;
        }
    }

    fbl::unique_ptr<SparsePaver> paver;
    zx_status_t status = SparsePaver::Create(path, slice_size_, disk_offset, disk_size, &paver);
    if (status != ZX_OK) {
        return status;
    }

    for (uint32_t i = 0; i < image_.partition_count; i++) {
        if ((partitions_[i].descriptor.flags & fvm::kSparseFlagZxcrypt) != 0) {
            //TODO(planders): Remove this error when we can create zxcrypt'd FVMs on host.
            printf("SparseContainer::Pave: zxcrypt not yet implemented for host-side FVM\n");
            return ZX_ERR_NOT_SUPPORTED;
        }

        if ((status = paver->AddPartition(&partitions_[i], reader_.get())) != ZX_OK) {
            return status;
        }
    }

    return paver->Commit();
}

size_t SparseContainer::SliceSize() const {
    return image_.slice_size;
}

size_t SparseContainer::SliceCount() const {
    CheckValid();
    size_t slices = 0;

    for (unsigned i = 0; i < image_.partition_count; i++) {
        if ((partitions_[i].descriptor.flags & fvm::kSparseFlagZxcrypt) != 0) {
            slices += kZxcryptExtraSlices;
        }

        for (unsigned j = 0; j < partitions_[i].descriptor.extent_count; j++) {
            slices += partitions_[i].extents[j].slice_count;
        }
    }

    return slices;
}

zx_status_t SparseContainer::AddPartition(const char* path, const char* type_name) {
    fbl::unique_ptr<Format> format;
    zx_status_t status;

    if ((status = Format::Create(path, type_name, &format)) != ZX_OK) {
        fprintf(stderr, "Failed to initialize partition\n");
        return status;
    }

    if ((status = AllocatePartition(std::move(format))) != ZX_OK) {
        fprintf(stderr, "Sparse partition allocation failed\n");
        return status;
    }

    return ZX_OK;
}

zx_status_t SparseContainer::Decompress(const char* path) {
    if ((flags_ & fvm::kSparseFlagLz4) == 0) {
        fprintf(stderr, "Cannot decompress un-compressed sparse file\n");
        return ZX_ERR_NOT_SUPPORTED;
    }

    fbl::unique_fd fd;

    fd.reset(open(path, O_WRONLY | O_CREAT | O_EXCL, 0644));
    if (!fd) {
        fprintf(stderr, "could not open %s: %s\n", path, strerror(errno));
        return ZX_ERR_IO;
    }

    return reader_->WriteDecompressed(std::move(fd));
}

zx_status_t SparseContainer::AllocatePartition(fbl::unique_ptr<Format> format) {
    partition_info_t partition;
    format->GetPartitionInfo(&partition.descriptor);
    partition.descriptor.magic = fvm::kPartitionDescriptorMagic;
    partition.descriptor.extent_count = 0;
    image_.header_length += sizeof(fvm::partition_descriptor_t);
    uint32_t part_index = image_.partition_count;

    zx_status_t status;
    if ((status = format->MakeFvmReady(SliceSize(), part_index)) != ZX_OK) {
        return status;
    }

    partitions_.push_back(std::move(partition));

    if (++image_.partition_count != partitions_.size()) {
        fprintf(stderr, "Unexpected number of partitions\n");
        return ZX_ERR_INTERNAL;
    }

    vslice_info_t vslice_info;
    unsigned i = 0;
    while ((status = format->GetVsliceRange(i++, &vslice_info)) == ZX_OK) {
        if ((status = AllocateExtent(part_index,
                                     vslice_info.vslice_start / format->BlocksPerSlice(),
                                     vslice_info.slice_count,
                                     vslice_info.block_count * format->BlockSize())) != ZX_OK) {
            return status;
        }
    }

    // This is expected if we have read all the other slices.
    if (status != ZX_ERR_OUT_OF_RANGE) {
        return status;
    }

    partitions_[part_index].format = std::move(format);
    return ZX_OK;
}

zx_status_t SparseContainer::AllocateExtent(uint32_t part_index, uint64_t slice_start,
                                            uint64_t slice_count, uint64_t extent_length) {
    if (part_index >= image_.partition_count) {
        fprintf(stderr, "Partition is not yet allocated\n");
        return ZX_ERR_OUT_OF_RANGE;
    }

    partition_info_t* partition = &partitions_[part_index];
    fvm::extent_descriptor_t extent;
    extent.magic = fvm::kExtentDescriptorMagic;
    extent.slice_start = slice_start;
    extent.slice_count = slice_count;
    extent.extent_length = extent_length;
    partition->extents.push_back(extent);

    if (partition->extents.size() != ++partition->descriptor.extent_count) {
        fprintf(stderr, "Unexpected number of extents\n");
        return ZX_ERR_INTERNAL;
    }

    image_.header_length += sizeof(fvm::extent_descriptor_t);
    extent_size_ += extent_length;
    dirty_ = true;
    return ZX_OK;
}

zx_status_t SparseContainer::PrepareWrite(size_t max_len) {
    if ((flags_ & fvm::kSparseFlagLz4) == 0) {
        return ZX_OK;
    }

    return compression_.Setup(max_len);
}

zx_status_t SparseContainer::WriteData(const void* data, size_t length) {
    if ((flags_ & fvm::kSparseFlagLz4) != 0) {
        return compression_.Compress(data, length);
    } else if (write(fd_.get(), data, length) != length) {
        return ZX_ERR_IO;
    }

    return ZX_OK;
}

zx_status_t SparseContainer::CompleteWrite() {
    if ((flags_ & fvm::kSparseFlagLz4) == 0) {
        return ZX_OK;
    }

    zx_status_t status = compression_.Finish();

    if (status != ZX_OK) {
        return status;
    }

    if (write(fd_.get(), compression_.GetData(), compression_.GetLength())
        != compression_.GetLength()) {
        return ZX_ERR_IO;
    }

    return ZX_OK;
}

void SparseContainer::CheckValid() const {
    if (!valid_) {
        fprintf(stderr, "Error: Sparse container is invalid\n");
        exit(-1);
    }
}