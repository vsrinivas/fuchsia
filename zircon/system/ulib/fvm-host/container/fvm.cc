// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <errno.h>
#include <inttypes.h>
#include <lib/fit/defer.h>
#include <sys/ioctl.h>

#include <iostream>
#include <memory>
#include <utility>

#include <fvm/format.h>
#include <fvm/sparse-reader.h>

#include "fvm-host/container.h"

#if defined(__APPLE__)
#include <sys/disk.h>
#define IOCTL_GET_BLOCK_COUNT DKIOCGETBLOCKCOUNT
#endif

#if defined(__linux__)
#include <linux/fs.h>

#define IOCTL_GET_BLOCK_COUNT BLKGETSIZE
#endif

constexpr int DEFAULT_OPEN_MODE = 0644;

zx_status_t FvmContainer::CreateNew(const char* path, size_t slice_size, off_t offset, off_t length,
                                    std::unique_ptr<FvmContainer>* out) {
  std::unique_ptr<FvmContainer> fvmContainer(new FvmContainer(path, slice_size, offset, length));

  zx_status_t status;
  if ((status = fvmContainer->InitNew()) != ZX_OK) {
    return status;
  }

  *out = std::move(fvmContainer);
  return ZX_OK;
}

zx_status_t FvmContainer::CreateExisting(const char* path, off_t offset,
                                         std::unique_ptr<FvmContainer>* out) {
  std::unique_ptr<FvmContainer> fvmContainer(new FvmContainer(path, 0, offset, 0));

  zx_status_t status;
  if ((status = fvmContainer->InitExisting()) != ZX_OK) {
    return status;
  }

  *out = std::move(fvmContainer);
  return ZX_OK;
}

zx_status_t FvmContainer::Verify(const char* path, off_t offset) {
  std::unique_ptr<FvmContainer> fvmContainer(new FvmContainer(path, 0, offset, 0));
  return fvmContainer->InitExisting(InitExistingMode::kCheckOnly);
}

FvmContainer::FvmContainer(const char* path, size_t slice_size, off_t offset, off_t length)
    : Container(path, slice_size, 0), disk_offset_(offset), disk_size_(length) {}

FvmContainer::~FvmContainer() = default;

zx_status_t FvmContainer::InitNew() {
  fd_.reset(open(path_.data(), O_RDWR, DEFAULT_OPEN_MODE));
  if (!fd_) {
    if (errno != ENOENT) {
      fprintf(stderr, "Failed to open path %s: %s\n", path_.data(), strerror(errno));
      return ZX_ERR_IO;
    }

    if (disk_offset_ > 0 || disk_size_ > 0) {
      fprintf(stderr, "Invalid disk size for path %s", path_.data());
      return ZX_ERR_INVALID_ARGS;
    }

    fd_.reset(open(path_.data(), O_RDWR | O_CREAT | O_EXCL, DEFAULT_OPEN_MODE));

    if (!fd_) {
      fprintf(stderr, "Failed to create path %s\n", path_.data());
      return ZX_ERR_IO;
    }

    xprintf("Created path %s\n", path_.data());
  } else {
    // If the file already exists, check its size and make sure it is valid given the user
    // provided disk size and offset (if any).
    uint64_t size;
    zx_status_t status = VerifyFileSize(&size);
    if (status != ZX_OK) {
      return status;
    }

    if (disk_size_ == 0) {
      disk_size_ = size;
    }
  }

  return info_.Reset(disk_size_, slice_size_);
}

zx_status_t FvmContainer::VerifyFileSize(uint64_t* size_out, bool allow_resize) {
  struct stat stats;
  if (fstat(fd_.get(), &stats) < 0) {
    fprintf(stderr, "Failed to stat %s\n", path_.data());
    return ZX_ERR_IO;
  }

  uint64_t size = stats.st_size;

  if (S_ISBLK(stats.st_mode)) {
    uint64_t block_count;
    if (ioctl(fd_.get(), IOCTL_GET_BLOCK_COUNT, &block_count) >= 0) {
      size = block_count * 512;
    }
  }

  if (allow_resize) {
    uint64_t minimum_disk_size = CalculateDiskSize();
    if (size < minimum_disk_size) {
      fprintf(stderr, "Invalid file size %" PRIu64 " for minimum disk size %" PRIu64 "\n", size,
              minimum_disk_size);
      return ZX_ERR_INVALID_ARGS;
    }
  } else if (disk_size_ > 0 && size < disk_offset_ + disk_size_) {
    fprintf(stderr, "Invalid file size %" PRIu64 " for specified offset+length\n", size);
    return ZX_ERR_INVALID_ARGS;
  }

  if (size_out != nullptr) {
    *size_out = size;
  }

  return ZX_OK;
}

zx_status_t FvmContainer::InitExisting(InitExistingMode mode) {
  int flag = mode == InitExistingMode::kAllowModification ? O_RDWR : O_RDONLY;
  fd_.reset(open(path_.data(), flag, DEFAULT_OPEN_MODE));

  if (!fd_) {
    fprintf(stderr, "Failed to open path %s: %s\n", path_.data(), strerror(errno));
    return ZX_ERR_IO;
  }

  fvm::Header fvm_superblock;
  if (pread(fd_.get(), &fvm_superblock, sizeof(fvm::Header), disk_offset_) != sizeof(fvm::Header)) {
    fprintf(stderr, "Failed to read FVM metadata from disk\n");
    return ZX_ERR_IO;
  }

  if (fvm_superblock.magic != fvm::kMagic) {
    fprintf(stderr, "Found invalid FVM container\n");
    return ZX_ERR_INVALID_ARGS;
  }

  disk_size_ = fvm_superblock.fvm_partition_size;

  // Attempt to load metadata from disk
  fvm::host::FdWrapper wrapper = fvm::host::FdWrapper(fd_.get());
  zx_status_t status = info_.Load(&wrapper, disk_offset_, disk_size_);
  if (status != ZX_OK) {
    return status;
  }

  if (!info_.IsValid()) {
    fprintf(stderr, "Found invalid FVM container\n");
    return ZX_ERR_INVALID_ARGS;
  }

  slice_size_ = info_.SliceSize();

  // For an existing file, ensure the metadata is internally consistent.
  // This includes accounting for the possibility the FVM may resize even if the
  // container is smaller than we expect.
  status = VerifyFileSize(nullptr, true /* allow_resize */);
  if (status != ZX_OK) {
    return status;
  }

  return ZX_OK;
}

zx_status_t FvmContainer::Verify() const {
  info_.CheckValid();

  zx_status_t status = info_.Validate();
  if (status != ZX_OK) {
    return status;
  }

  fvm::Header* sb = info_.SuperBlock();

  xprintf("Total size is %zu\n", disk_size_);
  xprintf("Metadata size is %zu\n", info_.MetadataSize());
  xprintf("Slice size is %" PRIu64 "\n", info_.SliceSize());
  xprintf("Slice count is %" PRIu64 "\n", info_.SuperBlock()->pslice_count);

  off_t start = 0;
  off_t end = disk_offset_ + info_.MetadataSize() * 2;
  size_t slice_index = 1;
  for (size_t vpart_index = 1; vpart_index < fvm::kMaxVPartitions; ++vpart_index) {
    fvm::VPartitionEntry* vpart = nullptr;
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
      std::cerr << vpart->name() << " fsck returned an error." << std::endl;
      return status;
    }

    xprintf("Found valid %s partition\n", vpart->name);
  }

  return ZX_OK;
}

zx_status_t FvmContainer::Extend(size_t new_disk_size) {
  if (disk_offset_) {
    fprintf(stderr, "Cannot extend FVM within another container\n");
    return ZX_ERR_BAD_STATE;
  }

  if (new_disk_size <= disk_size_) {
    if (extend_length_type_ == ExtendLengthType::LOWER_BOUND) {
      return ResizeImageFileToDiskSize();
    }
    fprintf(stderr, "Cannot extend to disk size %zu smaller than current size %" PRIu64 "\n",
            new_disk_size, disk_size_);
    return ZX_ERR_INVALID_ARGS;
  }

  const char* temp = ".tmp";

  if (path_.length() >= PATH_MAX - strlen(temp) - 1) {
    fprintf(stderr, "Path name exceeds maximum length\n");
    return ZX_ERR_INVALID_ARGS;
  }

  fbl::StringBuffer<PATH_MAX> path;
  path.AppendPrintf("%s%s", path_.c_str(), temp);
  fbl::unique_fd fd(open(path.c_str(), O_RDWR | O_CREAT, DEFAULT_OPEN_MODE));

  if (!fd) {
    fprintf(stderr, "Unable to open temp file %s\n", path.c_str());
    return ZX_ERR_IO;
  }

  auto cleanup = fit::defer([path]() {
    if (unlink(path.c_str()) < 0) {
      fprintf(stderr, "Failed to unlink path %s\n", path.c_str());
    }
  });

  if (ftruncate(fd.get(), new_disk_size) != 0) {
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
  fvm::Header source_header =
      fvm::Header::FromDiskSize(fvm::kMaxUsablePartitions, disk_size_, slice_size_);
  fvm::Header target_header =
      fvm::Header::FromDiskSize(fvm::kMaxUsablePartitions, new_disk_size, slice_size_);
  std::vector<uint8_t> data(slice_size_);
  for (uint32_t index = 1; index <= pslice_count; index++) {
    zx_status_t status;
    fvm::SliceEntry* slice = nullptr;
    if ((status = info_.GetSlice(index, &slice)) != ZX_OK) {
      fprintf(stderr, "Failed to retrieve slice %u\n", index);
      return status;
    }

    if (slice->IsFree()) {
      continue;
    }

    ssize_t r = pread(fd_.get(), data.data(), slice_size_, source_header.GetSliceDataOffset(index));
    if (r < 0 || static_cast<size_t>(r) != slice_size_) {
      fprintf(stderr, "Failed to read data from FVM: %ld\n", r);
      return ZX_ERR_BAD_STATE;
    }

    r = pwrite(fd.get(), data.data(), slice_size_, target_header.GetSliceDataOffset(index));
    if (r < 0 || static_cast<size_t>(r) != slice_size_) {
      fprintf(stderr, "Failed to write data to FVM: %ld\n", r);
      return ZX_ERR_BAD_STATE;
    }
  }

  size_t metadata_size = target_header.GetMetadataUsedBytes();
  if (zx_status_t status = info_.Grow(metadata_size); status != ZX_OK) {
    return status;
  }

  fvm::host::FdWrapper wrapper = fvm::host::FdWrapper(fd.get());
  if (zx_status_t status = info_.Write(&wrapper, 0, new_disk_size); status != ZX_OK) {
    return status;
  }

  fd_.reset(fd.release());
  disk_size_ = new_disk_size;

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

    fvm::Header header =
        fvm::Header::FromSliceCount(fvm::kMaxUsablePartitions, CountAddedSlices(), slice_size_);
    if (zx_status_t status = info_.Grow(header.GetMetadataAllocatedBytes()); status != ZX_OK) {
      return status;
    }

    uint64_t total_size = header.fvm_partition_size;
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

  non_empty_segments_ = {{disk_offset_, disk_offset_ + 2 * info_.MetadataSize()}};
  for (unsigned i = 0; i < partitions_.size(); i++) {
    if ((status = WritePartition(i)) != ZX_OK) {
      return status;
    }
  }

  xprintf("Successfully wrote FVM data to disk\n");
  return ZX_OK;
}

zx_status_t FvmContainer::ResizeImageFileToFit() {
  // Resize the image file to just fit the header and added partitions. Disk size specified in
  // the metadata header remains the same. Metadatasize and slice offset stay consistent with the
  // the specified disk size.
  size_t required_data_size = CountAddedSlices() * slice_size_;
  size_t minimal_size = disk_offset_ + required_data_size + 2 * info_.MetadataSize();
  if (ftruncate(fd_.get(), minimal_size) != 0) {
    fprintf(stderr, "Failed to truncate fvm container");
    return ZX_ERR_IO;
  }
  return ZX_OK;
}

zx_status_t FvmContainer::ResizeImageFileToDiskSize() {
  if (ftruncate(fd_.get(), disk_size_ + disk_offset_) != 0) {
    fprintf(stderr, "Failed to truncate fvm container");
    return ZX_ERR_IO;
  }
  return ZX_OK;
}

size_t FvmContainer::SliceSize() const {
  info_.CheckValid();
  return slice_size_;
}

zx_status_t FvmContainer::AddPartition(const char* path, const char* type_name,
                                       FvmReservation* reserve) {
  info_.CheckValid();
  std::unique_ptr<Format> format;
  zx_status_t status;
  if ((status = Format::Create(path, type_name, &format)) != ZX_OK) {
    fprintf(stderr, "Failed to initialize partition\n");
    return status;
  }

  uint32_t vpart_index;
  uint8_t guid[fvm::kGuidSize];
  format->Guid(guid);
  fvm::PartitionDescriptor descriptor;
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

    uint32_t vslice = static_cast<uint32_t>(vslice_info.vslice_start / format->BlocksPerSlice());

    for (unsigned i = 0; i < vslice_info.slice_count; i++) {
      uint32_t pslice;

      if ((status = info_.AllocateSlice(format->VpartIndex(), vslice + i, &pslice)) != ZX_OK) {
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

  fvm::VPartitionEntry* entry;
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

size_t FvmContainer::CountAddedSlices() const {
  size_t required_slices = 0;

  for (size_t index = 1; index < fvm::kMaxVPartitions; index++) {
    fvm::VPartitionEntry* vpart;
    ZX_ASSERT(info_.GetPartition(index, &vpart) == ZX_OK);

    if (vpart->slices == 0) {
      break;
    }

    required_slices += vpart->slices;
  }
  return required_slices;
}

uint64_t FvmContainer::CalculateDiskSize() const {
  info_.CheckValid();
  return CalculateDiskSizeForSlices(CountAddedSlices());
}

uint64_t FvmContainer::GetDiskSize() const { return disk_size_; }

zx_status_t FvmContainer::WritePartition(unsigned part_index) {
  info_.CheckValid();
  if (part_index > partitions_.size()) {
    fprintf(stderr, "Error: Tried to access partition %u / %zu\n", part_index, partitions_.size());
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

  auto slice_start = GetBlockStart(*pslice, 0, format->BlockSize());
  auto slice_end = slice_start + vslice_info.slice_count * slice_size_;
  AddNonEmptySegment(slice_start, slice_end);

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
        if ((status = format->FillBlock(vslice_info.block_offset + current_block)) != ZX_OK) {
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
  if (block_offset * block_size > slice_size_) {
    fprintf(stderr, "Not enough space in slice\n");
    return ZX_ERR_OUT_OF_RANGE;
  }

  if (lseek(fd_.get(), GetBlockStart(pslice, block_offset, block_size), SEEK_SET) < 0) {
    return ZX_ERR_BAD_STATE;
  }

  ssize_t r = write(fd_.get(), data, block_size);
  if (r < 0 || static_cast<size_t>(r) != block_size) {
    fprintf(stderr, "Failed to write data to FVM\n");
    return ZX_ERR_BAD_STATE;
  }

  return ZX_OK;
}

size_t FvmContainer::GetBlockStart(uint32_t pslice, uint32_t block_offset,
                                   size_t block_size) const {
  return disk_offset_ +
         fvm::Header::FromDiskSize(fvm::kMaxUsablePartitions, disk_size_, slice_size_)
             .GetSliceDataOffset(pslice) +
         block_offset * block_size;
}

AndroidSparseChunkType FvmContainer::DetermineAndroidSparseChunkType(const uint32_t* buffer,
                                                                     size_t block_size,
                                                                     size_t block_start) {
  // Check whether it can be dont-care block.
  // If it intersects with any non-empty segment, it cannot be dont-care.
  for (const auto& segment : non_empty_segments_) {
    if (segment.start >= block_start + block_size) {
      break;
    }
    if (segment.end > block_start) {
      if (std::all_of(buffer, &buffer[block_size / sizeof(buffer[0])],
                      [&](uint32_t val) { return val == buffer[0]; })) {
        return kChunkTypeFill;
      }
      return kChunkTypeRaw;
    }
  }
  return kChunkTypeDontCare;
}

namespace {
bool CanAppendBlockToChunk(const uint32_t* buffer, uint16_t block_type,
                           const AndroidSparseChunkHeader& chunk, uint32_t fill_val) {
  // Check whether we can just append the block to current chunk.
  // 1. The block has to be of the same type as the current chunk.
  // 2. If it is kChunkTypeFill, the fill value should be the same as well.
  if (block_type == chunk.chunk_type) {
    return block_type == kChunkTypeFill ? buffer[0] == fill_val : true;
  }
  return false;
}
}  // namespace

zx_status_t FvmContainer::ConvertToAndroidSparseImage() {
  char path[] = "/tmp/block.XXXXXX";
  fbl::unique_fd fd(mkstemp(path));
  if (!fd) {
    fprintf(stderr, "Failed to create temporary file\n");
    return ZX_ERR_IO;
  }

  auto cleanup = fit::defer([path]() {
    if (unlink(path) < 0) {
      fprintf(stderr, "Failed to unlink path %s\n", path);
    }
  });

  // The block size is recommended to be always 4096.
  constexpr size_t block_size = 4096;
  // Defined as uint32_t instead of uint8_t because kChunkTypeFill is based
  // on uint32_t granularity instead of byte.
  uint32_t buffer[block_size / sizeof(uint32_t)];

  if (lseek(fd_.get(), 0, SEEK_SET) != 0) {
    fprintf(stderr, "Failed to seek to the beginning of the file.\n");
    return ZX_ERR_IO;
  }

  uint64_t file_size = fvm::host::FdWrapper(fd_.get()).Size();
  FinalizeNonEmptySegmentsInfo();
  // Scan the file to determine all chunks.
  std::vector<AndroidSparseChunkHeader> chunks;
  size_t total_bytes = 0, total_blocks = 0;
  uint32_t fill_val = 0;
  while (total_bytes < file_size) {
    ssize_t read_bytes = read(fd_.get(), buffer, block_size);
    if (read_bytes != block_size) {
      fprintf(stderr, "Failed to read data @ %zu\n", total_bytes);
      return ZX_ERR_IO;
    }

    AndroidSparseChunkType block_type =
        DetermineAndroidSparseChunkType(buffer, block_size, total_bytes);
    if (!chunks.empty() && CanAppendBlockToChunk(buffer, block_type, chunks.back(), fill_val)) {
      chunks.back().chunk_blocks++;
      chunks.back().total_size += block_type == kChunkTypeRaw ? block_size : 0;
    } else {
      // Start a new chunk.
      chunks.push_back({block_type, 0, 1, sizeof(AndroidSparseChunkHeader)});
      if (block_type == kChunkTypeFill) {
        chunks.back().total_size += sizeof(uint32_t);
        fill_val = buffer[0];
      } else if (block_type == kChunkTypeRaw) {
        chunks.back().total_size += block_size;
      }
    }
    total_bytes += read_bytes;
    total_blocks++;
  }

  // Construct android sparse image header.
  AndroidSparseHeader sparse_header{
      .file_header_size = sizeof(AndroidSparseHeader),
      .chunk_header_size = sizeof(AndroidSparseChunkHeader),
      .block_size = static_cast<uint32_t>(block_size),
      .total_blocks = static_cast<uint32_t>(total_blocks),
      .total_chunks = static_cast<unsigned>(chunks.size()),
      .image_checksum = 0,
  };
  if (write(fd.get(), &sparse_header, sizeof(sparse_header)) != sizeof(sparse_header)) {
    fprintf(stderr, "Failed to write sparse header\n");
    return ZX_ERR_IO;
  }

  // Write chunks to file.
  size_t read_offset = 0;
  for (auto& chunk : chunks) {
    // Write chunk header.
    if (write(fd.get(), &chunk, sizeof(chunk)) != sizeof(chunk)) {
      fprintf(stderr, "Failed to write chunk header\n");
      return ZX_ERR_IO;
    }

    // Write chunk data.
    if (chunk.chunk_type == kChunkTypeRaw) {
      for (size_t i = 0; i < chunk.chunk_blocks; i++) {
        ssize_t read_bytes = pread(fd_.get(), buffer, block_size, read_offset + i * block_size);
        if (read_bytes != block_size) {
          fprintf(stderr, "Failed to read raw block data @ %zu\n", read_offset + i * block_size);
          return ZX_ERR_IO;
        }
        ssize_t write_bytes = write(fd.get(), buffer, block_size);
        if (write_bytes != block_size) {
          fprintf(stderr, "Failed to write raw block data\n");
          return ZX_ERR_IO;
        }
      }
    } else if (chunk.chunk_type == kChunkTypeFill) {
      uint32_t fill_val;
      if (pread(fd_.get(), &fill_val, sizeof(fill_val), read_offset) != sizeof(fill_val)) {
        fprintf(stderr, "Failed to read fill value @ %zu\n", read_offset);
        return ZX_ERR_IO;
      }
      if (write(fd.get(), &fill_val, sizeof(fill_val)) != sizeof(fill_val)) {
        fprintf(stderr, "Failed to write fill value  for fill chunk\n");
        return ZX_ERR_IO;
      }
    }
    read_offset += chunk.chunk_blocks * block_size;
  }

  fd_.reset(fd.release());
  if (rename(path, path_.c_str()) < 0) {
    fprintf(stderr, "Failed to copy over temp file\n");
    return ZX_ERR_IO;
  }

  cleanup.cancel();
  return ZX_OK;
}

namespace {
zx_status_t CreateCompressionContext(CompressionContext* out, size_t size) {
  auto result = CompressionContext::Create();
  if (!result.is_ok()) {
    fprintf(stderr, "%s", result.take_error_result().error.c_str());
    return ZX_ERR_INTERNAL;
  }
  *out = std::move(result.take_ok_result().value);
  out->Setup(size);
  return ZX_OK;
}
}  // namespace

zx_status_t FvmContainer::CompressWithLZ4() {
  constexpr size_t kBufferLength = 1024 * 1024;
  std::vector<uint8_t> buffer(kBufferLength);

  if (lseek(fd_.get(), 0, SEEK_SET) != 0) {
    fprintf(stderr, "Failed to seek to beginning of the file.\n");
    return ZX_ERR_IO;
  }

  uint64_t file_size = fvm::host::FdWrapper(fd_.get()).Size();
  CompressionContext compression;
  if (auto status = CreateCompressionContext(&compression, file_size); status != ZX_OK) {
    return status;
  }

  while (true) {
    ssize_t read_bytes = read(fd_.get(), buffer.data(), kBufferLength);
    if (read_bytes < 0) {
      fprintf(stderr, "Failed to read data from image file\n");
      return ZX_ERR_IO;
    } else if (read_bytes == 0) {
      break;
    }

    if (auto status = compression.Compress(buffer.data(), read_bytes); status != ZX_OK) {
      fprintf(stderr, "Failed to compress data.\n");
      return status;
    }
  }

  if (auto status = compression.Finish(); status != ZX_OK) {
    return status;
  }

  if (lseek(fd_.get(), 0, SEEK_SET) != 0) {
    fprintf(stderr, "Failed to seek to beginning of the file.\n");
    return ZX_ERR_IO;
  }

  // Write compressed data to file;
  const uint8_t* start = static_cast<const uint8_t*>(compression.GetData());
  const uint8_t* end = start + compression.GetLength();
  while (start < end) {
    ssize_t result = write(fd_.get(), start, end - start);
    if (result <= 0) {
      fprintf(stderr, "Failed to write compressed data to output file.\n");
      return ZX_ERR_IO;
    }
    start += result;
  }

  if (ftruncate(fd_.get(), compression.GetLength())) {
    fprintf(stderr, "Failed to truncate file\n");
    return ZX_ERR_IO;
  }

  return ZX_OK;
}

void FvmContainer::AddNonEmptySegment(size_t start, size_t end) {
  non_empty_segments_.push_back({start, end});
}

void FvmContainer::FinalizeNonEmptySegmentsInfo() {
  // 1. Sort segments
  // 2. Make sure segments are disjoint.
  std::sort(non_empty_segments_.begin(), non_empty_segments_.end(),
            [](auto& l, auto& r) { return l.start < r.start; });

  std::vector<Segment> disjoint;
  for (auto& seg : non_empty_segments_) {
    if (disjoint.empty() || disjoint.back().end < seg.start) {
      disjoint.push_back(seg);
    } else {
      disjoint.back().end = seg.end;
    }
  }
  non_empty_segments_ = disjoint;
}
