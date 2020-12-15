// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/fvm/snapshot_metadata.h"

#include <lib/zx/status.h>
#include <zircon/types.h>

#include <memory>
#include <vector>

#include <fbl/span.h>
#include <safemath/checked_math.h>

#include "src/storage/fvm/fvm.h"
#include "src/storage/fvm/snapshot_metadata_format.h"

namespace fvm {

namespace {

// Returns a byte view of a fixed size struct.
template <typename T>
fbl::Span<const uint8_t> FixedSizeStructToSpan(const T& typed_content) {
  return fbl::Span<const uint8_t>(reinterpret_cast<const uint8_t*>(&typed_content), sizeof(T));
}

// Returns a byte view of an array of fixed size structs.
template <typename T>
fbl::Span<const uint8_t> ArrayToSpan(const T* typed_content, size_t count) {
  return fbl::Span<const uint8_t>(reinterpret_cast<const uint8_t*>(typed_content),
                                  count * sizeof(T));
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

size_t SnapshotMetadata::BytesNeeded(const SnapshotMetadataHeader& header) {
  return header.AllocatedMetadataBytes();
}

SnapshotMetadata::SnapshotMetadata(std::unique_ptr<MetadataBuffer> data,
                                   SnapshotMetadataCopy active_header)
    : data_(std::move(data)), active_header_(active_header) {}

SnapshotMetadata::SnapshotMetadata(SnapshotMetadata&& o) noexcept { MoveFrom(std::move(o)); }

SnapshotMetadata& SnapshotMetadata::operator=(SnapshotMetadata&& o) noexcept {
  MoveFrom(std::move(o));
  return *this;
}

void SnapshotMetadata::MoveFrom(SnapshotMetadata&& o) {
  data_ = std::move(o.data_);
  active_header_ = o.active_header_;
}

void SnapshotMetadata::UpdateHash() {
  SnapshotMetadataHeader* header = &GetHeader();
  bzero(header->hash, sizeof(header->hash));
  digest::Digest digest;
  const uint8_t* hash = digest.Hash(header, header->AllocatedMetadataBytes());
  memcpy(header->hash, hash, sizeof(header->hash));
}

size_t SnapshotMetadata::GetInactiveHeaderOffset() const {
  return GetHeader().HeaderOffset(inactive_header());
}

void SnapshotMetadata::SwitchActiveHeaders() { active_header_ = OppositeHeader(active_header_); }

SnapshotMetadataHeader& SnapshotMetadata::GetHeader() const {
  return *reinterpret_cast<SnapshotMetadataHeader*>(static_cast<uint8_t*>(data_->data()));
}

PartitionSnapshotState& SnapshotMetadata::GetPartitionStateEntry(size_t idx) const {
  const SnapshotMetadataHeader& header = GetHeader();
  ZX_ASSERT(idx < header.PartitionStateTableNumEntries());
  return reinterpret_cast<PartitionSnapshotState*>(reinterpret_cast<uint8_t*>(data_->data()) +
                                                   header.PartitionStateTableOffset())[idx];
}

SnapshotExtentType& SnapshotMetadata::GetExtentTypeEntry(size_t idx) const {
  const SnapshotMetadataHeader& header = GetHeader();
  ZX_ASSERT(idx < header.ExtentTypeTableNumEntries());
  return reinterpret_cast<SnapshotExtentType*>(reinterpret_cast<uint8_t*>(data_->data()) +
                                               header.ExtentTypeTableOffset())[idx];
}

const MetadataBuffer* SnapshotMetadata::Get() const { return data_.get(); }

zx::status<SnapshotMetadata> SnapshotMetadata::Create(std::unique_ptr<MetadataBuffer> data_a,
                                                      std::unique_ptr<MetadataBuffer> data_b) {
  if (data_a->size() < sizeof(SnapshotMetadataHeader)) {
    return zx::error(ZX_ERR_BUFFER_TOO_SMALL);
  }
  if (data_b->size() < sizeof(SnapshotMetadataHeader)) {
    return zx::error(ZX_ERR_BUFFER_TOO_SMALL);
  }

  // For now just assume header is valid. It may contain nonsense, but PickValidHeader will check
  // this, and we can at least check that the offset is reasonable so we don't overflow now.
  const auto* header = reinterpret_cast<const SnapshotMetadataHeader*>(data_a->data());
  size_t meta_size = header->AllocatedMetadataBytes();
  if (meta_size > data_a->size() || meta_size > data_b->size()) {
    fprintf(stderr, "fvm: SnapshotMetadata (%lu bytes) too large for buffers (%lu and %lu bytes)\n",
            meta_size, data_a->size(), data_b->size());
    return zx::error(ZX_ERR_IO_DATA_INTEGRITY);
  }
  std::optional<SnapshotMetadataCopy> active_header =
      PickValid(data_a.get(), data_b.get(), meta_size);
  if (!active_header) {
    return zx::error(ZX_ERR_IO_DATA_INTEGRITY);
  }

  std::unique_ptr<MetadataBuffer> data;
  switch (active_header.value()) {
    case SnapshotMetadataCopy::kPrimary:
      data = std::move(data_a);
      break;
    case SnapshotMetadataCopy::kSecondary:
      data = std::move(data_b);
      break;
  }
  return zx::ok(SnapshotMetadata(std::move(data), active_header.value()));
}

zx::status<SnapshotMetadata> SnapshotMetadata::Synthesize(const PartitionSnapshotState* partitions,
                                                          size_t num_partitions,
                                                          const SnapshotExtentType* extents,
                                                          size_t num_extents) {
  SnapshotMetadataHeader header(num_partitions, num_extents);
  if (header.PartitionStateTableNumEntries() < num_partitions + 1 ||
      header.ExtentTypeTableNumEntries() < num_extents) {
    return zx::error(ZX_ERR_INVALID_ARGS);
  }

  size_t buffer_size = BytesNeeded(header);
  std::unique_ptr<uint8_t[]> buf(new uint8_t[buffer_size]);

  // TODO(fxbug.dev/59980) The first entries in the partition state tables must be unused.
  // Remove this after we support zero-indexing.
  std::vector<PartitionSnapshotState> actual_partitions(0);
  if (num_partitions > 0) {
    ZX_ASSERT(partitions != nullptr);
    actual_partitions = std::vector<PartitionSnapshotState>(num_partitions + 1);
    actual_partitions[0].Release();
    for (size_t i = 0; i < num_partitions; ++i) {
      actual_partitions[i + 1] = partitions[i];
    }
  }

  const fbl::Span<const uint8_t> header_span = FixedSizeStructToSpan(header);
  const fbl::Span<const uint8_t> partitions_span = ContainerToSpan(actual_partitions);
  const fbl::Span<const uint8_t> extents_span = ArrayToSpan(extents, num_extents);

  auto write_metadata = [&](size_t offset, size_t sz, const fbl::Span<const uint8_t>& span) {
    ZX_ASSERT(offset + sz <= buffer_size);
    ZX_ASSERT(sz >= span.size());
    if (!span.empty()) {
      memcpy(buf.get() + offset, span.data(), span.size());
    }
    bzero(buf.get() + offset + span.size(), sz - span.size());
  };

  write_metadata(0, fvm::kSnapshotMetadataHeaderMaxSize, header_span);
  write_metadata(header.PartitionStateTableOffset(), header.PartitionStateTableSizeBytes(),
                 partitions_span);
  write_metadata(header.ExtentTypeTableOffset(), header.ExtentTypeTableSizeBytes(), extents_span);

  SnapshotMetadata metadata(std::make_unique<HeapMetadataBuffer>(std::move(buf), buffer_size),
                            SnapshotMetadataCopy::kPrimary);
  metadata.UpdateHash();
  return zx::ok(std::move(metadata));
}

bool SnapshotMetadata::CheckHash(const void* metadata, size_t meta_size) {
  ZX_ASSERT(meta_size >= sizeof(SnapshotMetadataHeader));
  const auto* header = static_cast<const SnapshotMetadataHeader*>(metadata);
  fbl::Span<const uint8_t> meta_span(static_cast<const uint8_t*>(metadata), meta_size);
  fbl::Span<const uint8_t> before_hash =
      meta_span.subspan(0, offsetof(SnapshotMetadataHeader, hash));
  fbl::Span<const uint8_t> after_hash =
      meta_span.subspan(offsetof(SnapshotMetadataHeader, hash) + sizeof(header->hash));

  uint8_t empty_hash[sizeof(header->hash)];
  bzero(empty_hash, sizeof(empty_hash));

  digest::Digest digest;
  digest.Init();
  digest.Update(before_hash.data(), before_hash.size());
  digest.Update(empty_hash, sizeof(empty_hash));
  digest.Update(after_hash.data(), after_hash.size());
  digest.Final();
  return digest == header->hash;
}

std::optional<SnapshotMetadataCopy> SnapshotMetadata::PickValid(const void* a, const void* b,
                                                                size_t meta_size) {
  const auto& header_a = *reinterpret_cast<const SnapshotMetadataHeader*>(a);
  const auto& header_b = *reinterpret_cast<const SnapshotMetadataHeader*>(b);

  std::string header_error;

  bool a_valid = false;
  if (header_a.IsValid(header_error)) {
    if (CheckHash(&header_a, meta_size)) {
      a_valid = true;
    } else {
      fprintf(stderr, "fvm: Primary snapshot meta has invalid content hash\n");
    }
  } else {
    fprintf(stderr, "fvm: Primary snapshot meta is invalid: %s\n", header_error.c_str());
  }
  header_error.clear();
  bool b_valid = false;
  if (header_b.IsValid(header_error)) {
    if (CheckHash(&header_b, meta_size)) {
      b_valid = true;
    } else {
      fprintf(stderr, "fvm: Secondary snapshot meta has invalid content hash\n");
    }
  } else {
    fprintf(stderr, "fvm: Secondary snapshot meta is invalid: %s\n", header_error.c_str());
  }

  // Decide if we should use the primary or the b copy of snapshot metadata.
  if (!a_valid && !b_valid) {
    return std::nullopt;
  }
  if (a_valid && !b_valid) {
    return SnapshotMetadataCopy::kPrimary;
  }
  if (!a_valid && b_valid) {
    return SnapshotMetadataCopy::kSecondary;
  }

  // Both valid, pick the newest.
  return header_a.generation >= header_b.generation ? SnapshotMetadataCopy::kPrimary
                                                    : SnapshotMetadataCopy::kSecondary;
}

}  // namespace fvm
