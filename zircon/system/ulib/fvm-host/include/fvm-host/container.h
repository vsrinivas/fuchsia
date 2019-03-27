// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fcntl.h>
#include <lz4/lz4frame.h>
#include <string.h>

#include <fbl/auto_call.h>
#include <fbl/string_buffer.h>
#include <fbl/unique_fd.h>
#include <fbl/vector.h>
#include <fvm/fvm-sparse.h>
#include <fvm/sparse-reader.h>

#include "file-wrapper.h"
#include "format.h"
#include "fvm-info.h"
#include "sparse-paver.h"

// The number of additional slices a partition will need to become zxcrypt'd.
// TODO(planders): Replace this with a value supplied by ulib/zxcrypt.
constexpr size_t kZxcryptExtraSlices = 1;

// A Container represents a method of storing multiple file system partitions in an
// FVM-recognizable format
class Container {
public:
    // Returns a Container representation of the FVM within the given |path|, starting at |offset|
    // bytes of length |length| bytes. Will return an error if the file does not exist or is not a
    // valid Container type, or if flags is not zero or a valid combination of fvm::sparse_flags_t.
    static zx_status_t Create(const char* path, off_t offset, off_t length, uint32_t flags,
                              fbl::unique_ptr<Container>* out);

    Container(const char* path, size_t slice_size, uint32_t flags);

    virtual ~Container();

    // Resets the Container state so we are ready to add a new set of partitions
    // Init must be called separately from the constructor, as it will overwrite data pertinent to
    // an existing Container.
    virtual zx_status_t Init() = 0;

    // Reports various information about the Container, e.g. number of partitions, and runs fsck on
    // all supported partitions (blobfs, minfs)
    virtual zx_status_t Verify() const = 0;

    // Commits the Container data to disk
    virtual zx_status_t Commit() = 0;

    // Returns the Container's specified slice size (in bytes)
    virtual size_t SliceSize() const = 0;

    // Given a path to a valid file system partition, adds that partition to the container
    virtual zx_status_t AddPartition(const char* path, const char* type_name,
                                     FvmReservation* reserve) = 0;

    // Calculates the minimum disk size required to hold the unpacked contents of the container.
    virtual uint64_t CalculateDiskSize() const = 0;

protected:
    // Returns the minimum disk size necessary to store |slice_count| slices of size |slice_size_|
    // in an FVM.
    uint64_t CalculateDiskSizeForSlices(size_t slice_count) const;

    fbl::StringBuffer<PATH_MAX> path_;
    fbl::unique_fd fd_;
    size_t slice_size_;
    uint32_t flags_;
};

class FvmContainer final : public Container {
    struct FvmPartitionInfo {
        uint32_t vpart_index;
        uint32_t pslice_start;
        uint32_t slice_count;
        fbl::unique_ptr<Format> format;
    };

public:
    // Creates an FVM container at the given path, creating a new file if one does not already
    // exist. |offset| and |length| are provided to specify the offset (in bytes) and the length
    // (in bytes) of the FVM within the file. For a file that has not yet been created, these
    // should both be 0. For a file that exists, if not otherwise specified the offset should be 0
    // and the length should be the size of the file.
    static zx_status_t Create(const char* path, size_t slice_size, off_t offset, off_t length,
                              fbl::unique_ptr<FvmContainer>* out);
    FvmContainer(const char* path, size_t slice_size, off_t offset, off_t length);
    ~FvmContainer();
    zx_status_t Init() final;
    zx_status_t Verify() const final;
    zx_status_t Commit() final;

    // Extends the FVM container to the specified length
    zx_status_t Extend(size_t length);
    size_t SliceSize() const final;
    zx_status_t AddPartition(const char* path, const char* type_name,
                             FvmReservation* reserve) final;

    uint64_t CalculateDiskSize() const final;

    // Returns the actual disk size.
    uint64_t GetDiskSize() const;

private:
    uint64_t disk_offset_;
    uint64_t disk_size_;
    fbl::Vector<FvmPartitionInfo> partitions_;
    FvmInfo info_;

    // Write the |part_index|th partition to disk
    zx_status_t WritePartition(unsigned part_index);
    // Write a partition's |extent_index|th extent to disk. |*pslice| is the starting pslice, and
    // is updated to reflect the latest written pslice.
    zx_status_t WriteExtent(unsigned extent_index, Format* format, uint32_t* pslice);
    // Write one data block of size |block_size| to disk at |block_offset| within pslice |pslice|
    zx_status_t WriteData(uint32_t pslice, uint32_t block_offset, size_t block_size, void* data);
};

class CompressionContext {
public:
    CompressionContext() {}
    ~CompressionContext() {}
    zx_status_t Setup(size_t max_len);
    zx_status_t Compress(const void* data, size_t length);
    zx_status_t Finish();

    const void* GetData() const { return data_.get(); }
    size_t GetLength() const { return offset_; }

private:
    void IncreaseOffset(size_t value) {
        offset_ += value;
        ZX_DEBUG_ASSERT(offset_ <= size_);
    }

    size_t GetRemaining() const {
        return size_ - offset_;
    }

    void* GetBuffer() const {
        return data_.get() + offset_;
    }

    void Reset(size_t size) {
        data_.reset(new uint8_t[size]);
        size_ = size;
        offset_ = 0;
    }

    LZ4F_compressionContext_t cctx_;
    fbl::unique_ptr<uint8_t[]> data_;
    size_t size_ = 0;
    size_t offset_ = 0;
};

class SparseContainer final : public Container {
public:
    static zx_status_t Create(const char* path, size_t slice_size, uint32_t flags,
                              fbl::unique_ptr<SparseContainer>* out);
    SparseContainer(const char* path, uint64_t slice_size, uint32_t flags);
    ~SparseContainer();
    zx_status_t Init() final;
    zx_status_t Verify() const final;
    zx_status_t Commit() final;

    // Unpacks the sparse container and "paves" it to the file system exposed by |wrapper|.
    zx_status_t Pave(fbl::unique_ptr<fvm::host::FileWrapper> wrapper,
                     size_t disk_offset = 0, size_t disk_size = 0);

    size_t SliceSize() const final;
    size_t SliceCount() const;
    zx_status_t AddPartition(const char* path, const char* type_name,
                             FvmReservation* reserve) final;

    // Decompresses the contents of the sparse file (if they are compressed), and writes the output
    // to |path|.
    zx_status_t Decompress(const char* path);

    uint64_t CalculateDiskSize() const final;

    // Checks whether the container will fit within a disk of size |target_size| (in bytes).
    zx_status_t CheckDiskSize(uint64_t target_size) const;

private:
    bool valid_;
    bool dirty_;
    size_t disk_size_;
    size_t extent_size_;
    fvm::sparse_image_t image_;
    fbl::Vector<SparsePartitionInfo> partitions_;
    CompressionContext compression_;
    fbl::unique_ptr<fvm::SparseReader> reader_;

    zx_status_t AllocatePartition(fbl::unique_ptr<Format> format, FvmReservation* reserve);
    zx_status_t AllocateExtent(uint32_t part_index, uint64_t slice_start, uint64_t slice_count,
                               uint64_t extent_length);

    zx_status_t PrepareWrite(size_t max_len);
    zx_status_t WriteData(const void* data, size_t length);
    zx_status_t CompleteWrite();
    void CheckValid() const;
};
