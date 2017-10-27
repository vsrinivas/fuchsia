// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>

#include <fbl/vector.h>
#include <fbl/unique_fd.h>
#include <fvm/fvm-sparse.h>

#include "../format/format.h"

// A Container represents a method of storing multiple file system partitions in an
// FVM-recognizable format
class Container {
public:
    // Returns a Container representation of the file at the given |path|. Will return an error if
    // the file does not exist or is not a valid Container type.
    static zx_status_t Create(const char* path, fbl::unique_ptr<Container>* out);

    Container(size_t slice_size)
        : dirty_(false), slice_size_(slice_size) {}
    virtual ~Container() {}

    // Resets the Container state so we are ready to add a new set of partitions
    // Init must be called separately from the constructor, as it will overwrite data pertinent to
    // an existing Container.
    virtual zx_status_t Init() = 0;

    // Reports various information about the Container, e.g. number of partitions, and runs fsck on
    // all supported partitions (blobstore, minfs)
    virtual zx_status_t Verify() const = 0;

    // Commits the Container data to disk
    virtual zx_status_t Commit() = 0;

    // Returns the Container's specified slice size (in bytes)
    virtual size_t SliceSize() const = 0;

    // Given a path to a valid file system partition, adds that partition to the container
    virtual zx_status_t AddPartition(const char* path, const char* type_name) = 0;

protected:
    fbl::unique_fd fd_;
    bool dirty_;
    size_t slice_size_;
};

class FvmContainer final : public Container {
public:
    static zx_status_t Create(const char* path, size_t slice_size,
                              fbl::unique_ptr<FvmContainer>* out);
    FvmContainer(const char* path, size_t slice_size);
    ~FvmContainer();
    zx_status_t Init() final;
    zx_status_t Verify() const final;
    zx_status_t Commit() final;
    size_t SliceSize() const final;
    zx_status_t AddPartition(const char* path, const char* type_name) final;

private:
    bool valid_;
    size_t metadata_size_;
    size_t disk_size_;
    size_t block_size_;
    size_t block_count_;
    uint32_t vpart_hint_;
    uint32_t pslice_hint_;
    fbl::unique_ptr<uint8_t[]> metadata_;

    void CheckValid() const;
    zx_status_t AllocatePartition(uint8_t* type, uint8_t* guid, const char* name, uint32_t slices,
                                  uint32_t* vpart_index);
    zx_status_t AllocateSlice(uint32_t vpart, uint32_t vslice, uint32_t* pslice);
    zx_status_t WriteExtent(unsigned vslice_count, Format* format);
    zx_status_t WriteData(uint32_t vpart, uint32_t pslice, void* data, uint32_t block_offset,
                          size_t block_size);
};

class SparseContainer final : public Container {
    typedef struct {
        fvm::partition_descriptor_t descriptor;
        fbl::Vector<fvm::extent_descriptor_t> extents;
        fbl::unique_ptr<Format> format;
    } partition_info_t;

public:
    static zx_status_t Create(const char* path, size_t slice_size,
                              fbl::unique_ptr<SparseContainer>* out);
    SparseContainer(const char* path, uint64_t slice_size);
    ~SparseContainer();
    zx_status_t Init() final;
    zx_status_t Verify() const final;
    zx_status_t Commit() final;
    size_t SliceSize() const final;
    zx_status_t AddPartition(const char* path, const char* type_name) final;

private:
    fvm::sparse_image_t image_;
    fbl::Vector<partition_info_t> partitions_;

    zx_status_t AllocatePartition(fbl::unique_ptr<Format> format);
    zx_status_t AllocateExtent(uint32_t part_index, uint64_t slice_start, uint64_t slice_count,
                               uint64_t extent_length);
};
