// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zircon/compiler.h>

#include <fbl/array.h>
#include <src/lib/chunked-compression/chunked-archive.h>
#include <src/lib/chunked-compression/status.h>
#include <src/lib/fxl/logging.h>

namespace chunked_compression {

// SeekTable

size_t SeekTable::CompressedSize() const {
  size_t sz = SerializedHeaderSize();

  size_t biggest_offset = 0;
  for (const SeekTableEntry& entry : entries_) {
    if (entry.compressed_offset >= biggest_offset) {
      sz = entry.compressed_offset + entry.compressed_size;
      biggest_offset = entry.compressed_offset;
    }
  }

  return sz;
}

size_t SeekTable::SerializedHeaderSize() const {
  return kChunkArchiveSeekTableOffset + (entries_.size() * sizeof(SeekTableEntry));
}

size_t SeekTable::DecompressedSize() const {
  size_t sz = 0;

  size_t biggest_offset = 0;
  for (const SeekTableEntry& entry : entries_) {
    if (entry.decompressed_offset >= biggest_offset) {
      sz = entry.decompressed_offset + entry.decompressed_size;
      biggest_offset = entry.decompressed_offset;
    }
  }

  return sz;
}

std::optional<unsigned> SeekTable::EntryForCompressedOffset(size_t offset) const {
  for (unsigned i = 0; i < entries_.size(); ++i) {
    const SeekTableEntry& entry = entries_[i];
    if (entry.compressed_offset <= offset &&
        offset < entry.compressed_offset + entry.compressed_size) {
      return i;
    }
  }
  return std::nullopt;
}

std::optional<unsigned> SeekTable::EntryForDecompressedOffset(size_t offset) const {
  for (unsigned i = 0; i < entries_.size(); ++i) {
    const SeekTableEntry& entry = entries_[i];
    if (entry.decompressed_offset <= offset &&
        offset < entry.decompressed_offset + entry.decompressed_size) {
      return i;
    }
  }
  return std::nullopt;
}

// HeaderReader

Status HeaderReader::Parse(const void* data, size_t len, size_t file_length, SeekTable* out) {
  if (!data || !out) {
    return kStatusErrInvalidArgs;
  } else if (len < kChunkArchiveMinHeaderSize) {
    return kStatusErrBufferTooSmall;
  } else if (file_length < len) {
    return kStatusErrInvalidArgs;
  }
  Status status;
  if ((status = CheckMagic(static_cast<const uint8_t*>(data), len)) != kStatusOk) {
    return status;
  } else if ((status = CheckVersion(static_cast<const uint8_t*>(data), len)) != kStatusOk) {
    return status;
  }
  fbl::Array<SeekTableEntry> table;
  if ((status = ParseSeekTable(static_cast<const uint8_t*>(data), len, file_length, &table)) !=
      kStatusOk) {
    return status;
  }

  out->entries_ = std::move(table);

  return kStatusOk;
}

Status HeaderReader::CheckMagic(const uint8_t* data, size_t len) {
  if (len < sizeof(ArchiveMagicType)) {
    return kStatusErrIoDataIntegrity;
  }
  // In practice the magic is always at the start of the header, but for consistency with other
  // accesses we offset |data| by |kChunkArchiveMagicOffset|.
  const ArchiveMagicType& magic =
      reinterpret_cast<const ArchiveMagicType*>(data + kChunkArchiveMagicOffset)[0];
  return magic == kChunkedCompressionArchiveMagic ? kStatusOk : kStatusErrIoDataIntegrity;
}

Status HeaderReader::CheckVersion(const uint8_t* data, size_t len) {
  if (len < sizeof(ArchiveVersionType)) {
    return kStatusErrIoDataIntegrity;
  }
  const ArchiveVersionType& version =
      reinterpret_cast<const ArchiveVersionType*>(data + kChunkArchiveVersionOffset)[0];
  if (version != kVersion) {
    FXL_LOG(ERROR) << "Unsupported archive version " << version << ", expected " << kVersion;
    return kStatusErrInvalidArgs;
  }
  return kStatusOk;
}

Status HeaderReader::GetNumChunks(const uint8_t* data, size_t len, ChunkCountType* num_chunks_out) {
  if (len < kChunkArchiveNumChunksOffset + sizeof(ChunkCountType)) {
    return kStatusErrIoDataIntegrity;
  }
  *num_chunks_out = reinterpret_cast<const ChunkCountType*>(data + kChunkArchiveNumChunksOffset)[0];
  return kStatusOk;
}

Status HeaderReader::ParseSeekTable(const uint8_t* data, size_t len, size_t file_length,
                                    fbl::Array<SeekTableEntry>* entries_out) {
  ChunkCountType num_chunks;
  Status status = GetNumChunks(data, len, &num_chunks);
  if (status != kStatusOk) {
    return status;
  }
  size_t header_end = kChunkArchiveSeekTableOffset + (num_chunks * sizeof(SeekTableEntry));
  if (len < header_end) {
    FXL_LOG(ERROR) << "Invalid archive. Header too small for seek table size";
    return kStatusErrIoDataIntegrity;
  }

  const SeekTableEntry* entries =
      reinterpret_cast<const SeekTableEntry*>(data + kChunkArchiveSeekTableOffset);
  fbl::Array<SeekTableEntry> table(new SeekTableEntry[num_chunks], num_chunks);
  for (unsigned i = 0; i < num_chunks; ++i) {
    table[i] = entries[i];
  }

  if ((status = CheckSeekTable(table, header_end, file_length)) != kStatusOk) {
    return status;
  }

  *entries_out = std::move(table);
  return kStatusOk;
}

Status HeaderReader::CheckSeekTable(const fbl::Array<SeekTableEntry>& table, size_t header_end,
                                    size_t file_length) {
  for (unsigned i = 0; i < table.size(); ++i) {
    const SeekTableEntry* prev = i > 0 ? &table[i - 1] : nullptr;
    Status status;
    if ((status = CheckSeekTableEntry(table[i], prev, header_end, file_length)) != kStatusOk) {
      FXL_LOG(ERROR) << "Invalid archive. Bad seek table entry " << i;
      return status;
    }
  }
  return kStatusOk;
}

Status HeaderReader::CheckSeekTableEntry(const SeekTableEntry& entry, const SeekTableEntry* prev,
                                         size_t header_end, size_t file_length) {
  if (entry.compressed_size == 0 || entry.decompressed_size == 0) {
    // Invariant I4
    FXL_LOG(ERROR) << "Zero-sized entry";
    return kStatusErrIoDataIntegrity;
  } else if (entry.compressed_offset < header_end) {
    // Invariant I1
    FXL_LOG(ERROR) << "Invalid archive. Chunk overlaps with header";
    return kStatusErrIoDataIntegrity;
  }
  uint64_t compressed_end;
  if (add_overflow(entry.compressed_offset, entry.compressed_size, &compressed_end)) {
    FXL_LOG(ERROR) << "Compressed frame too big";
    return kStatusErrIoDataIntegrity;
  } else if (compressed_end > file_length) {
    // Invariant I5
    FXL_LOG(ERROR) << "Invalid archive. Chunk exceeds file length";
    return kStatusErrIoDataIntegrity;
  }
  __UNUSED uint64_t decompressed_end;
  if (add_overflow(entry.decompressed_offset, entry.decompressed_size, &decompressed_end)) {
    FXL_LOG(ERROR) << "Decompressed frame too big";
    return kStatusErrIoDataIntegrity;
  }
  if (prev != nullptr) {
    if (prev->decompressed_offset + prev->decompressed_size != entry.decompressed_offset) {
      // Invariant I2
      FXL_LOG(ERROR) << "Invalid archive. Decompressed chunks are non-contiguous";
      return kStatusErrIoDataIntegrity;
    }
    if (prev->compressed_offset + prev->compressed_size > entry.compressed_offset) {
      // Invariant I3
      FXL_LOG(ERROR) << "Invalid archive. Chunks are non-monotonic";
      return kStatusErrIoDataIntegrity;
    }
  } else if (entry.decompressed_offset != 0) {
    // Invariant I0
    FXL_LOG(ERROR) << "Invalid archive. Decompressed chunks must start at offset 0";
    return kStatusErrIoDataIntegrity;
  }
  return kStatusOk;
}

// HeaderWriter

HeaderWriter::HeaderWriter(void* dst, size_t dst_len, size_t num_frames)
    : dst_(static_cast<uint8_t*>(dst)) {
  ZX_ASSERT(num_frames < kChunkArchiveMaxFrames);
  ZX_ASSERT(dst_len >= kChunkArchiveSeekTableOffset + (num_frames * sizeof(SeekTableEntry)));
  num_frames_ = static_cast<ChunkCountType>(num_frames);
  entries_ = reinterpret_cast<SeekTableEntry*>(dst_ + kChunkArchiveSeekTableOffset);
}

Status HeaderWriter::AddEntry(const SeekTableEntry& entry) {
  if (current_frame_ == num_frames_) {
    return kStatusErrBadState;
  }

  size_t header_end = kChunkArchiveSeekTableOffset + (num_frames_ * sizeof(SeekTableEntry));
  const SeekTableEntry* prev = current_frame_ > 0 ? &entries_[current_frame_ - 1] : nullptr;
  // Since we don't know yet how long the compressed file will be, simply pass UINT64_MAX
  // as the upper bound for the file length. This effectively disables checking compressed frames
  // against the file size.
  Status status = HeaderReader::CheckSeekTableEntry(entry, prev, header_end, UINT64_MAX);
  if (status != kStatusOk) {
    return kStatusErrInvalidArgs;
  }

  entries_[current_frame_] = entry;
  ++current_frame_;
  return kStatusOk;
}

Status HeaderWriter::Finalize() {
  if (current_frame_ < num_frames_) {
    return kStatusErrBadState;
  }

  // In practice the magic is always at the start of the header, but for consistency with other
  // accesses we offset |data| by |kChunkArchiveMagicOffset|.
  reinterpret_cast<ArchiveMagicType*>(dst_ + kChunkArchiveMagicOffset)[0] =
      kChunkedCompressionArchiveMagic;
  reinterpret_cast<ArchiveVersionType*>(dst_ + kChunkArchiveVersionOffset)[0] = kVersion;
  reinterpret_cast<uint16_t*>(dst_ + kChunkArchiveReservedOffset)[0] = 0u;
  reinterpret_cast<ChunkCountType*>(dst_ + kChunkArchiveNumChunksOffset)[0] = num_frames_;

  return kStatusOk;
}

}  // namespace chunked_compression
