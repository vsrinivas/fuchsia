// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/fvm/metadata.h"

#include <lib/stdcompat/span.h>
#include <lib/zx/status.h>
#include <zircon/assert.h>
#include <zircon/types.h>

#include <memory>
#include <optional>
#include <vector>

#include <safemath/checked_math.h>

#include "src/storage/fvm/format.h"
#include "src/storage/fvm/fvm.h"

namespace fvm {

namespace {

// Returns a byte view of a fixed size struct.
template <typename T>
cpp20::span<const uint8_t> FixedSizeStructToSpan(const T& typed_content) {
  return cpp20::span<const uint8_t>(reinterpret_cast<const uint8_t*>(&typed_content), sizeof(T));
}

// Returns a byte view of an array of structs.
template <typename T>
cpp20::span<const uint8_t> ContainerToSpan(const T& container) {
  if (container.empty()) {
    return cpp20::span<const uint8_t>();
  }
  return cpp20::span<const uint8_t>(reinterpret_cast<const uint8_t*>(container.data()),
                                    container.size() * sizeof(*container.data()));
}

}  // namespace

size_t Metadata::BytesNeeded(const fvm::Header& header) {
  return header.GetMetadataAllocatedBytes();
}

Metadata::Metadata(std::unique_ptr<MetadataBuffer> data, SuperblockType active_header)
    : data_(std::move(data)), active_header_(active_header) {}

Metadata::Metadata(Metadata&& o) noexcept { MoveFrom(std::move(o)); }

Metadata& Metadata::operator=(Metadata&& o) noexcept {
  MoveFrom(std::move(o));
  return *this;
}

void Metadata::MoveFrom(Metadata&& o) {
  data_ = std::move(o.data_);
  active_header_ = o.active_header_;
}

bool Metadata::CheckValidity(uint64_t disk_size, uint64_t disk_block_size) const {
  std::string header_err;
  bool valid = GetHeader().IsValid(disk_size, disk_block_size, header_err);
  if (!valid) {
    fprintf(stderr, "Invalid header: %s\n", header_err.c_str());
  }
  return valid;
}

void Metadata::UpdateHash() {
  ::fvm::UpdateHash(static_cast<uint8_t*>(data_->data()), GetHeader().GetMetadataUsedBytes());
}

size_t Metadata::GetInactiveHeaderOffset() const {
  return GetHeader().GetSuperblockOffset(inactive_header());
}

void Metadata::SwitchActiveHeaders() { active_header_ = OppositeHeader(active_header_); }

Header& Metadata::GetHeader() const {
  return *reinterpret_cast<Header*>(static_cast<uint8_t*>(data_->data()));
}

VPartitionEntry& Metadata::GetPartitionEntry(size_t idx) const {
  const Header& header = GetHeader();
  size_t offset = MetadataOffset(SuperblockType::kPrimary) + header.GetPartitionEntryOffset(idx);
  ZX_ASSERT(offset + sizeof(VPartitionEntry) <= data_->size());
  if (idx > header.GetPartitionTableEntryCount()) {
    fprintf(stderr,
            "fatal: Accessing out-of-bounds partition (idx %zu, table has %lu usable entries)\n",
            idx, header.GetPartitionTableEntryCount());
    ZX_ASSERT(idx <= header.GetPartitionTableEntryCount());
  }
  return *reinterpret_cast<VPartitionEntry*>(reinterpret_cast<uint8_t*>(data_->data()) + offset);
}

SliceEntry& Metadata::GetSliceEntry(size_t idx) const {
  const Header& header = GetHeader();
  size_t offset = MetadataOffset(SuperblockType::kPrimary) + header.GetSliceEntryOffset(idx);
  ZX_ASSERT(offset + sizeof(SliceEntry) <= data_->size());
  if (idx > header.GetAllocationTableUsedEntryCount()) {
    fprintf(stderr,
            "fatal: Accessing out-of-bounds slice (idx %zu, table has %lu usable entries)\n", idx,
            header.GetAllocationTableUsedEntryCount());
    ZX_ASSERT(idx <= header.GetAllocationTableUsedEntryCount());
  }
  return *reinterpret_cast<SliceEntry*>(reinterpret_cast<uint8_t*>(data_->data()) + offset);
}

size_t Metadata::MetadataOffset(SuperblockType type) const {
  return GetHeader().GetSuperblockOffset(type);
}

const MetadataBuffer* Metadata::Get() const { return data_.get(); }

zx::result<Metadata> Metadata::CopyWithNewDimensions(const Header& dimensions) const {
  if (BytesNeeded(dimensions) < data_->size()) {
    return zx::error(ZX_ERR_BUFFER_TOO_SMALL);
  }
  const Header& header = GetHeader();
  if (dimensions.fvm_partition_size < header.fvm_partition_size ||
      dimensions.GetPartitionTableEntryCount() < header.GetPartitionTableEntryCount() ||
      dimensions.GetAllocationTableUsedEntryCount() < header.GetAllocationTableUsedEntryCount() ||
      dimensions.GetAllocationTableAllocatedEntryCount() <
          header.GetAllocationTableAllocatedEntryCount()) {
    return zx::error(ZX_ERR_BUFFER_TOO_SMALL);
  }

  Header new_header = header;
  new_header.fvm_partition_size = dimensions.fvm_partition_size;
  new_header.pslice_count = dimensions.pslice_count;
  new_header.vpartition_table_size = dimensions.vpartition_table_size;
  new_header.allocation_table_size = dimensions.allocation_table_size;

  // TODO(fxbug.dev/59980) The first entries in the partition/slice tables must be unused.
  // |Synthesize()| expects an array that does *not* include the empty zero entries.
  // Remove this after we support zero-indexing.
  const VPartitionEntry* partitions = nullptr;
  size_t num_partitions = header.GetPartitionTableEntryCount();
  if (num_partitions <= 1) {
    // Both 0 and 1 partitions count as having no partitions to copy.
    num_partitions = 0;
  } else {
    partitions = &GetPartitionEntry(1u);
  }
  const SliceEntry* slices = nullptr;
  size_t num_slices = header.GetAllocationTableUsedEntryCount();
  if (num_slices <= 1) {
    // Both 0 and 1 slices count as having no slices to copy.
    num_slices = 0;
  } else {
    slices = &GetSliceEntry(1u);
  }

  return Synthesize(new_header, partitions, num_partitions, slices, num_slices);
}

zx::result<Metadata> Metadata::Create(std::unique_ptr<MetadataBuffer> data_a,
                                      std::unique_ptr<MetadataBuffer> data_b) {
  return Create(std::numeric_limits<uint64_t>::max(), kBlockSize, std::move(data_a),
                std::move(data_b));
}

zx::result<Metadata> Metadata::Create(size_t disk_size, size_t disk_block_size,
                                      std::unique_ptr<MetadataBuffer> data_a,
                                      std::unique_ptr<MetadataBuffer> data_b) {
  if (data_a->size() < sizeof(Header)) {
    return zx::error(ZX_ERR_BUFFER_TOO_SMALL);
  }
  if (data_b->size() < sizeof(Header)) {
    return zx::error(ZX_ERR_BUFFER_TOO_SMALL);
  }

  // For now just assume header is valid. It may contain nonsense, but PickValidHeader will check
  // this, and we can at least check that the offset is reasonable so we don't overflow now.
  const Header* header = reinterpret_cast<const Header*>(data_a->data());
  size_t meta_size = header->GetMetadataAllocatedBytes();
  if (meta_size > data_a->size() || meta_size > data_b->size()) {
    fprintf(stderr, "fvm: Metadata (%lu bytes) too large for buffers (%lu and %lu bytes)\n",
            meta_size, data_a->size(), data_b->size());
    return zx::error(ZX_ERR_IO_DATA_INTEGRITY);
  }
  std::optional<SuperblockType> active_header =
      PickValidHeader(disk_size, disk_block_size, data_a->data(), data_b->data(), meta_size);
  if (!active_header) {
    return zx::error(ZX_ERR_IO_DATA_INTEGRITY);
  }

  std::unique_ptr<MetadataBuffer> data;
  switch (active_header.value()) {
    case SuperblockType::kPrimary:
      data = std::move(data_a);
      break;
    case SuperblockType::kSecondary:
      data = std::move(data_b);
      break;
  }
  return zx::ok(Metadata(std::move(data), active_header.value()));
}

zx::result<Metadata> Metadata::Synthesize(const fvm::Header& header,
                                          const VPartitionEntry* partitions, size_t num_partitions,
                                          const SliceEntry* slices, size_t num_slices) {
  if (num_partitions > header.GetPartitionTableEntryCount()) {
    return zx::error(ZX_ERR_INVALID_ARGS);
  }
  if (num_slices > header.GetAllocationTableUsedEntryCount()) {
    return zx::error(ZX_ERR_INVALID_ARGS);
  }
  if (header.slice_size == 0) {
    return zx::error(ZX_ERR_INVALID_ARGS);
  }

  size_t buffer_size = BytesNeeded(header);
  std::unique_ptr<uint8_t[]> buf(new uint8_t[buffer_size]);

  // TODO(fxbug.dev/59980) The first entries in the partition/slice tables must be unused.
  // Remove this after we support zero-indexing.
  std::vector<VPartitionEntry> actual_partitions(0);
  if (num_partitions > 0) {
    ZX_ASSERT(partitions != nullptr);
    actual_partitions = std::vector<VPartitionEntry>(num_partitions + 1);
    actual_partitions[0].Release();
    for (size_t i = 0; i < num_partitions; ++i) {
      actual_partitions[i + 1] = partitions[i];
    }
  }
  std::vector<SliceEntry> actual_slices(0);
  if (num_slices > 0) {
    ZX_ASSERT(slices != nullptr);
    actual_slices = std::vector<SliceEntry>(num_slices + 1);
    actual_slices[0].Release();
    for (size_t i = 0; i < num_slices; ++i) {
      actual_slices[i + 1] = slices[i];
    }
  }

  const cpp20::span<const uint8_t> header_span = FixedSizeStructToSpan(header);
  const cpp20::span<const uint8_t> partitions_span = ContainerToSpan(actual_partitions);
  const cpp20::span<const uint8_t> slices_span = ContainerToSpan(actual_slices);

  auto write_metadata = [&](size_t offset, size_t sz, const cpp20::span<const uint8_t>& span) {
    ZX_ASSERT(offset + sz <= buffer_size);
    ZX_ASSERT(sz >= span.size());
    if (!span.empty()) {
      memcpy(buf.get() + offset, span.data(), span.size());
    }
    bzero(buf.get() + offset + span.size(), sz - span.size());
  };

  write_metadata(0, fvm::kBlockSize, header_span);
  write_metadata(header.GetPartitionTableOffset(), header.GetPartitionTableByteSize(),
                 partitions_span);
  write_metadata(header.GetAllocationTableOffset(), header.GetAllocationTableAllocatedByteSize(),
                 slices_span);
  // TODO(fxbug.dev/59567): Synthesize snapshot metadata.

  ::fvm::UpdateHash(buf.get(), header.GetMetadataUsedBytes());

  Metadata metadata(std::make_unique<HeapMetadataBuffer>(std::move(buf), buffer_size),
                    SuperblockType::kPrimary);
  if (!metadata.CheckValidity()) {
    return zx::error(ZX_ERR_INVALID_ARGS);
  }
  return zx::ok(std::move(metadata));
}

}  // namespace fvm
