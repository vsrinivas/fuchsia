// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/zx/status.h>
#include <zircon/types.h>

#include <optional>
#include <vector>

#include <fbl/span.h>
#include <fvm/format.h>
#include <fvm/fvm.h>
#include <fvm/metadata.h>
#include <safemath/checked_math.h>

namespace fvm {

namespace {

// Returns a byte view of a fixed size struct.
template <typename T>
fbl::Span<const uint8_t> FixedSizeStructToSpan(const T& typed_content) {
  return fbl::Span<const uint8_t>(reinterpret_cast<const uint8_t*>(&typed_content), sizeof(T));
}

// Returns a byte view of an array of structs.
template <typename T>
fbl::Span<const uint8_t> ContainerToSpan(const T& container) {
  if (container.empty()) {
    return fbl::Span<const uint8_t>();
  }
  return fbl::Span<const uint8_t>(reinterpret_cast<const uint8_t*>(container.data()),
                                  container.size() * sizeof(*container.data()));
}

}  // namespace

size_t MetadataBuffer::BytesNeeded(const fvm::Header& header) {
  // Enough for both the A/B copies of metadata.
  // TODO(fxbug.dev/59567): Account for snapshot metadata.
  return 2 * header.GetMetadataAllocatedBytes();
}

HeapMetadataBuffer::HeapMetadataBuffer(std::unique_ptr<uint8_t[]> buffer, size_t size)
    : buffer_(std::move(buffer)), size_(size) {}

HeapMetadataBuffer::~HeapMetadataBuffer() = default;

std::unique_ptr<MetadataBuffer> HeapMetadataBuffer::Create(size_t size) const {
  return std::make_unique<HeapMetadataBuffer>(std::unique_ptr<uint8_t[]>(new uint8_t[size]), size);
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

bool Metadata::CheckValidity() const {
  std::optional<SuperblockType> active_header = ValidateHeader(
      reinterpret_cast<const uint8_t*>(data_->data()) + MetadataOffset(SuperblockType::kPrimary),
      reinterpret_cast<const uint8_t*>(data_->data()) + MetadataOffset(SuperblockType::kSecondary),
      GetHeader(active_header_).GetMetadataAllocatedBytes());
  // TODO(jfsulliv): should we be strict and fail if active_header_ (the instance field) is
  // inconsistent with active_header?
  return active_header != std::nullopt;
}

void Metadata::UpdateHash() {
  ::fvm::UpdateHash(static_cast<uint8_t*>(data_->data()) + MetadataOffset(SuperblockType::kPrimary),
                    GetHeader(SuperblockType::kPrimary).GetMetadataUsedBytes());
  ::fvm::UpdateHash(
      static_cast<uint8_t*>(data_->data()) + MetadataOffset(SuperblockType::kSecondary),
      GetHeader(SuperblockType::kSecondary).GetMetadataUsedBytes());
}

Header& Metadata::GetHeader(SuperblockType type) const {
  return *reinterpret_cast<Header*>(static_cast<uint8_t*>(data_->data()) + MetadataOffset(type));
}

VPartitionEntry& Metadata::GetPartitionEntry(SuperblockType type, unsigned idx) const {
  const Header& header = GetHeader(type);
  size_t offset = MetadataOffset(type) + header.GetPartitionEntryOffset(idx);
  ZX_ASSERT(offset + sizeof(VPartitionEntry) <= data_->size());
  if (idx > header.GetPartitionTableEntryCount()) {
    fprintf(stderr,
            "fatal: Accessing out-of-bounds partition (idx %u, table has %lu usable entries)\n",
            idx, header.GetPartitionTableEntryCount());
    ZX_ASSERT(idx <= header.GetPartitionTableEntryCount());
  }
  return *reinterpret_cast<VPartitionEntry*>(reinterpret_cast<uint8_t*>(data_->data()) + offset);
}

SliceEntry& Metadata::GetSliceEntry(SuperblockType type, unsigned idx) const {
  const Header& header = GetHeader(type);
  size_t offset = MetadataOffset(type) + header.GetSliceEntryOffset(idx);
  ZX_ASSERT(offset + sizeof(SliceEntry) <= data_->size());
  if (idx > header.GetAllocationTableUsedEntryCount()) {
    fprintf(stderr, "fatal: Accessing out-of-bounds slice (idx %u, table has %lu usable entries)\n",
            idx, header.GetAllocationTableUsedEntryCount());
    ZX_ASSERT(idx <= header.GetAllocationTableUsedEntryCount());
  }
  return *reinterpret_cast<SliceEntry*>(reinterpret_cast<uint8_t*>(data_->data()) + offset);
}

size_t Metadata::MetadataOffset(SuperblockType type) const {
  // Due to the secondary header being at a dynamic offset, we have to look at the primary
  // header's contents to find the secondary header. This is safe to do even if the primary
  // is partially corrupt, because otherwise the Metadata object would fail checks in
  // |Metadata::Create|.
  const Header* primary_header = reinterpret_cast<const Header*>(data_->data());
  return primary_header->GetSuperblockOffset(type);
}

const MetadataBuffer* Metadata::UnsafeGetRaw() const { return data_.get(); }

zx::status<Metadata> Metadata::CopyWithNewDimensions(const Header& dimensions) const {
  if (MetadataBuffer::BytesNeeded(dimensions) < data_->size()) {
    return zx::error(ZX_ERR_BUFFER_TOO_SMALL);
  }
  const Header& header = GetHeader(active_header_);
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
    partitions = &GetPartitionEntry(active_header_, 1);
  }
  const SliceEntry* slices = nullptr;
  size_t num_slices = header.GetAllocationTableUsedEntryCount();
  if (num_slices <= 1) {
    // Both 0 and 1 slices count as having no slices to copy.
    num_slices = 0;
  } else {
    slices = &GetSliceEntry(active_header_, 1);
  }

  return Synthesize(new_header, partitions, num_partitions, slices, num_slices);
}

zx::status<Metadata> Metadata::Create(std::unique_ptr<MetadataBuffer> data) {
  if (data->size() < sizeof(Header)) {
    return zx::error(ZX_ERR_BUFFER_TOO_SMALL);
  }
  const Header* primary_header = reinterpret_cast<const Header*>(data->data());

  // For now just assume primary_header is valid. It may contain nonsense, but ValidateHeader will
  // check this, and we can at least check that the offset is reasonable so we don't overflow now.
  size_t secondary_offset = primary_header->GetSuperblockOffset(SuperblockType::kSecondary);
  size_t meta_size = primary_header->GetMetadataAllocatedBytes();
  if (meta_size > data->size() || !safemath::CheckAdd(secondary_offset, meta_size).IsValid() ||
      secondary_offset + meta_size > data->size()) {
    fprintf(stderr, "fvm: Metadata (%lu bytes/copy) too large for buffer (%lu bytes)\n", meta_size,
            data->size());
    return zx::error(ZX_ERR_IO_DATA_INTEGRITY);
  }
  std::optional<SuperblockType> active_header = ValidateHeader(
      data->data(), reinterpret_cast<uint8_t*>(data->data()) + secondary_offset, meta_size);
  if (!active_header) {
    return zx::error(ZX_ERR_IO_DATA_INTEGRITY);
  }

  return zx::ok(Metadata(std::move(data), active_header.value()));
}

zx::status<Metadata> Metadata::Synthesize(const fvm::Header& header,
                                          const VPartitionEntry* partitions, size_t num_partitions,
                                          const SliceEntry* slices, size_t num_slices) {
  if (num_partitions > header.GetPartitionTableEntryCount() ||
      num_slices > header.GetAllocationTableUsedEntryCount() || header.slice_size == 0) {
    return zx::error(ZX_ERR_INVALID_ARGS);
  }

  size_t buffer_size = MetadataBuffer::BytesNeeded(header);
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

  const fbl::Span<const uint8_t> header_span = FixedSizeStructToSpan(header);
  const fbl::Span<const uint8_t> partitions_span = ContainerToSpan(actual_partitions);
  const fbl::Span<const uint8_t> slices_span = ContainerToSpan(actual_slices);

  auto write_metadata = [&](size_t offset, size_t sz, const fbl::Span<const uint8_t>& span) {
    ZX_ASSERT(offset + sz <= buffer_size);
    ZX_ASSERT(sz >= span.size());
    if (!span.empty()) {
      memcpy(buf.get() + offset, span.data(), span.size());
    }
    bzero(buf.get() + offset + span.size(), sz - span.size());
  };

  // Write the A copy.
  size_t a_offset = header.GetSuperblockOffset(SuperblockType::kPrimary);
  write_metadata(a_offset, fvm::kBlockSize, header_span);
  write_metadata(a_offset + header.GetPartitionTableOffset(), header.GetPartitionTableByteSize(),
                 partitions_span);
  write_metadata(a_offset + header.GetAllocationTableOffset(),
                 header.GetAllocationTableAllocatedByteSize(), slices_span);
  size_t b_offset = header.GetSuperblockOffset(SuperblockType::kSecondary);
  // Write the B copy.
  write_metadata(b_offset, fvm::kBlockSize, header_span);
  write_metadata(b_offset + header.GetPartitionTableOffset(), header.GetPartitionTableByteSize(),
                 partitions_span);
  write_metadata(b_offset + header.GetAllocationTableOffset(),
                 header.GetAllocationTableAllocatedByteSize(), slices_span);

  // TODO(fxbug.dev/59567): Synthesize snapshot metadata.

  ::fvm::UpdateHash(buf.get() + a_offset, header.GetMetadataUsedBytes());
  ::fvm::UpdateHash(buf.get() + b_offset, header.GetMetadataUsedBytes());

  return Create(std::make_unique<HeapMetadataBuffer>(std::move(buf), buffer_size));
}

}  // namespace fvm
