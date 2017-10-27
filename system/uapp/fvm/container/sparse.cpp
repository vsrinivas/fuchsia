// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "container.h"

zx_status_t SparseContainer::Create(const char* path, size_t slice_size,
                                    fbl::unique_ptr<SparseContainer>* out) {
    fbl::AllocChecker ac;
    fbl::unique_ptr<SparseContainer> sparseContainer(new (&ac) SparseContainer(path, slice_size));
    if (!ac.check()) {
        return ZX_ERR_NO_MEMORY;
    }

    zx_status_t status;
    if ((status = sparseContainer->Init()) != ZX_OK) {
        return status;
    }

    *out = fbl::move(sparseContainer);
    return ZX_OK;
}

SparseContainer::SparseContainer(const char* path, uint64_t slice_size)
    : Container(slice_size) {
    fd_.reset(open(path, O_CREAT | O_RDWR, 0666));

    if (!fd_) {
        printf("Failed to open sparse data path\n");
        exit(-1);
    }

    struct stat s;
    if (fstat(fd_.get(), &s) < 0) {
        printf("Failed to stat %s\n", path);
        exit(-1);
    }

    if (s.st_size > 0) {
        if (read(fd_.get(), &image_, sizeof(fvm::sparse_image_t)) != sizeof(fvm::sparse_image_t)) {
            fprintf(stderr, "SparseContainer: Failed to read the sparse header\n");
            exit(-1);
        }

        for (unsigned i = 0; i < image_.partition_count; i++) {
            partition_info_t partition;
            partitions_.push_back(fbl::move(partition));
            if (read(fd_.get(), &partitions_[i].descriptor, sizeof(fvm::partition_descriptor_t)) !=
                    sizeof(fvm::partition_descriptor_t)) {
                fprintf(stderr, "SparseContainer: Failed to read partition %u\n", i);
                exit(-1);
            }

            for (unsigned j = 0; j < partitions_[i].descriptor.extent_count; j++) {
                fvm::extent_descriptor_t extent;
                partitions_[i].extents.push_back(extent);
                if (read(fd_.get(), &partitions_[i].extents[j], sizeof(fvm::extent_descriptor_t)) !=
                        sizeof(fvm::extent_descriptor_t)) {
                    fprintf(stderr, "SparseContainer: Failed to read extent\n");
                    exit(-1);
                }
            }
        }

        printf("Successfully read from existing sparse data container.\n");
    }
}

SparseContainer::~SparseContainer() = default;

zx_status_t SparseContainer::Init() {
    image_.magic = fvm::kSparseFormatMagic;
    image_.version = fvm::kSparseFormatVersion;
    image_.slice_size = slice_size_;
    image_.partition_count = 0;
    image_.header_length = sizeof(fvm::sparse_image_t);
    partitions_.reset();
    dirty_ = true;
    printf("Initialized new sparse data container.\n");
    return ZX_OK;
}

zx_status_t SparseContainer::Verify() const {
    if (image_.magic != fvm::kSparseFormatMagic) {
        fprintf(stderr, "SparseContainer: Bad magic\n");
        return ZX_ERR_IO;
    }

    printf("Slice size is %" PRIu64 "\n", image_.slice_size);
    printf("Found %" PRIu64 " partitions\n", image_.partition_count);

    off_t start = 0;
    off_t end = image_.header_length;

    for (unsigned i = 0; i < image_.partition_count; i++) {
        fbl::Vector<size_t> extent_lengths;
        start = end;
        printf("Found partition %u with %u extents\n", i,
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

        if ((status = Format::Check(fbl::move(dupfd), start, end, extent_lengths, part)) != ZX_OK) {
            const char* name = reinterpret_cast<const char*>(partitions_[i].descriptor.name);
            fprintf(stderr, "%s fsck returned an error.\n", name);
            return status;
        }
    }
    return ZX_OK;
}

zx_status_t SparseContainer::Commit() {
    if (!dirty_ || image_.partition_count == 0) {
        printf("Commit: Nothing to write.\n");
        return ZX_OK;
    }

    // Recalculate and verify header length
    uint64_t header_length = 0;

    if (lseek(fd_.get(), 0, SEEK_SET) < 0) {
        printf("Seek reset failed\n");
        return ZX_ERR_IO;
    }

    header_length += sizeof(fvm::sparse_image_t);
    if (write(fd_.get(), &image_, sizeof(fvm::sparse_image_t)) != sizeof(fvm::sparse_image_t)) {
        printf("Write sparse image header failed\n");
        return ZX_ERR_IO;
    }

    for (unsigned i = 0; i < image_.partition_count; i++) {
        fvm::partition_descriptor_t partition = partitions_[i].descriptor;

        header_length += sizeof(fvm::partition_descriptor_t);
        if (write(fd_.get(), &partition, sizeof(fvm::partition_descriptor_t)) !=
                sizeof(fvm::partition_descriptor_t)) {
            printf("Write partition failed\n");
            return ZX_ERR_IO;
        }

        for (unsigned j = 0; j < partition.extent_count; j++) {
            fvm::extent_descriptor_t extent = partitions_[i].extents[j];
            header_length += sizeof(fvm::extent_descriptor_t);
            if (write(fd_.get(), &extent, sizeof(fvm::extent_descriptor_t)) !=
                    sizeof(fvm::extent_descriptor_t)) {
                printf("Write extent failed\n");
                return ZX_ERR_IO;
            }
        }
    }

    if (header_length != image_.header_length) {
        printf("Header length does not match!\n");
        return ZX_ERR_INTERNAL;
    }

    // Write each partition out to sparse file
    for (unsigned i = 0; i < image_.partition_count; i++) {
        fvm::partition_descriptor_t partition = partitions_[i].descriptor;
        Format* format = partitions_[i].format.get();

        vslice_info_t vslice_info;
        // Write out each extent in the partition
        for (unsigned j = 0; j < partition.extent_count; j++) {
            if (format->GetVsliceRange(j, &vslice_info) != ZX_OK) {
                printf("Unable to access partition extent\n");
                return ZX_ERR_OUT_OF_RANGE;
            }

            // Write out each block in the extent
            for (unsigned k = 0; k < vslice_info.block_count; k++) {
                if (format->FillBlock(vslice_info.block_offset + k) != ZX_OK) {
                    fprintf(stderr, "Failed to read block\n");
                    return ZX_ERR_IO;
                }

                if (write(fd_.get(), format->Data(), format->BlockSize()) !=
                    format->BlockSize()) {
                    fprintf(stderr, "Failed to write data to sparse file\n");
                    return ZX_ERR_IO;
                }
            }
        }
    }

    printf("Successfully wrote sparse data to disk.\n");
    return ZX_OK;
}

size_t SparseContainer::SliceSize() const {
    return image_.slice_size;
}

zx_status_t SparseContainer::AddPartition(const char* path, const char* type_name) {
    fbl::unique_ptr<Format> format;
    zx_status_t status;

    if ((status = Format::Create(path, type_name, &format)) != ZX_OK) {
        printf("Failed to initialize partition\n");
        return status;
    }

    if ((status = AllocatePartition(fbl::move(format))) != ZX_OK) {
        printf("Sparse partition allocation failed\n");
        return status;
    }

    return ZX_OK;
}

zx_status_t SparseContainer::AllocatePartition(fbl::unique_ptr<Format> format) {
    partition_info_t partition;
    partition.descriptor.magic = fvm::kPartitionDescriptorMagic;
    format->Type(partition.descriptor.type);
    format->Name(reinterpret_cast<char*>(partition.descriptor.name));
    partition.descriptor.extent_count = 0;
    //TODO(planders): flags?
    image_.header_length += sizeof(fvm::partition_descriptor_t);
    uint32_t part_index = image_.partition_count;

    zx_status_t status;
    if ((status = format->MakeFvmReady(SliceSize(), part_index)) != ZX_OK) {
        fprintf(stderr, "Failed to MakeFvmReady minfs partition\n");
        return status;
    }

    partitions_.push_back(fbl::move(partition));

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

    partitions_[part_index].format = fbl::move(format);

    return ZX_OK;
}

zx_status_t SparseContainer::AllocateExtent(uint32_t part_index, uint64_t slice_start,
                                            uint64_t slice_count, uint64_t extent_length) {
    if (part_index >= image_.partition_count) {
        printf("Partition is not yet allocated\n");
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
    dirty_ = true;
    return ZX_OK;
}
