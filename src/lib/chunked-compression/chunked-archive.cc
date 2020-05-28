// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/cksum.h>
#include <lib/syslog/cpp/macros.h>
#include <string.h>
#include <zircon/compiler.h>

#include <fbl/array.h>
#include <src/lib/chunked-compression/chunked-archive.h>
#include <src/lib/chunked-compression/status.h>

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

Status HeaderReader::Parse(const void* void_data, size_t len, size_t file_length, SeekTable* out) {
  if (!void_data || !out) {
    return kStatusErrInvalidArgs;
  } else if (len < kChunkArchiveMinHeaderSize) {
    return kStatusErrBufferTooSmall;
  } else if (file_length < len) {
    return kStatusErrInvalidArgs;
  }
  const uint8_t* data = static_cast<const uint8_t*>(void_data);
  Status status;
  if ((status = CheckMagic(data, len)) != kStatusOk) {
    return status;
  } else if ((status = CheckVersion(data, len)) != kStatusOk) {
    return status;
  }
  ChunkCountType num_chunks;
  if ((status = GetNumChunks(data, len, &num_chunks)) != kStatusOk) {
    return status;
  } else if (num_chunks > kChunkArchiveMaxFrames) {
    // It's possible that the num_chunks field was corrupted. Treat this as an integrity error.
    return kStatusErrIoDataIntegrity;
  }
  size_t expected_header_length =
      kChunkArchiveSeekTableOffset + (num_chunks * sizeof(SeekTableEntry));
  if (expected_header_length > len) {
    // Note that we can't distinguish between two cases:
    // - The client passed a truncated buffer.
    // - The num_chunks field was corrupted.
    // The second case will be caught by the checksum, so assume that the former case applies here.
    return kStatusErrBufferTooSmall;
  }
  // IMPORTANT: New fields should be parsed after the checksum is verified.
  // (The magic and num_chunks fields are necessary to parse first, so they are exceptions.)
  if ((status = CheckChecksum(data, expected_header_length)) != kStatusOk) {
    return status;
  }
  fbl::Array<SeekTableEntry> table;
  if ((status = ParseSeekTable(data, len, file_length, &table)) != kStatusOk) {
    return status;
  }

  out->entries_ = std::move(table);

  return kStatusOk;
}

Status HeaderReader::CheckMagic(const uint8_t* data, size_t len) {
  if (len < kArchiveMagicLength) {
    return kStatusErrBufferTooSmall;
  }
  // In practice the magic is always at the start of the header, but for consistency with other
  // accesses we offset |data| by |kChunkArchiveMagicOffset|.
  if (memcmp(data + kChunkArchiveMagicOffset, kChunkArchiveMagic, kArchiveMagicLength)) {
    FX_LOGS(ERROR) << "File magic doesn't match.";
    return kStatusErrIoDataIntegrity;
  }
  return kStatusOk;
}

Status HeaderReader::CheckVersion(const uint8_t* data, size_t len) {
  if (len < kChunkArchiveVersionOffset + sizeof(ArchiveVersionType)) {
    return kStatusErrBufferTooSmall;
  }
  const ArchiveVersionType& version =
      reinterpret_cast<const ArchiveVersionType*>(data + kChunkArchiveVersionOffset)[0];
  if (version != kVersion) {
    FX_LOGS(ERROR) << "Unsupported archive version " << version << ", expected " << kVersion;
    return kStatusErrInvalidArgs;
  }
  return kStatusOk;
}

Status HeaderReader::CheckChecksum(const uint8_t* data, size_t len) {
  if (len < kChunkArchiveHeaderCrc32Offset + sizeof(uint32_t)) {
    return kStatusErrBufferTooSmall;
  }
  uint32_t crc = reinterpret_cast<const uint32_t*>(data + kChunkArchiveHeaderCrc32Offset)[0];
  uint32_t expected_crc = ComputeChecksum(data, len);
  if (crc != expected_crc) {
    FX_LOGS(ERROR) << "Bad archive checksum";
    return kStatusErrIoDataIntegrity;
  }
  return kStatusOk;
}

Status HeaderReader::GetNumChunks(const uint8_t* data, size_t len, ChunkCountType* num_chunks_out) {
  if (len < kChunkArchiveNumChunksOffset + sizeof(ChunkCountType)) {
    return kStatusErrBufferTooSmall;
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
    FX_LOGS(ERROR) << "Invalid archive. Header too small for seek table size";
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
      FX_LOGS(ERROR) << "Invalid archive. Bad seek table entry " << i;
      return status;
    }
  }
  return kStatusOk;
}

Status HeaderReader::CheckSeekTableEntry(const SeekTableEntry& entry, const SeekTableEntry* prev,
                                         size_t header_end, size_t file_length) {
  if (entry.compressed_size == 0 || entry.decompressed_size == 0) {
    // Invariant I4
    FX_LOGS(ERROR) << "Zero-sized entry";
    return kStatusErrIoDataIntegrity;
  } else if (entry.compressed_offset < header_end) {
    // Invariant I1
    FX_LOGS(ERROR) << "Invalid archive. Chunk overlaps with header";
    return kStatusErrIoDataIntegrity;
  }
  uint64_t compressed_end;
  if (add_overflow(entry.compressed_offset, entry.compressed_size, &compressed_end)) {
    FX_LOGS(ERROR) << "Compressed frame too big";
    return kStatusErrIoDataIntegrity;
  } else if (compressed_end > file_length) {
    // Invariant I5
    FX_LOGS(ERROR) << "Invalid archive. Chunk exceeds file length";
    return kStatusErrIoDataIntegrity;
  }
  __UNUSED uint64_t decompressed_end;
  if (add_overflow(entry.decompressed_offset, entry.decompressed_size, &decompressed_end)) {
    FX_LOGS(ERROR) << "Decompressed frame too big";
    return kStatusErrIoDataIntegrity;
  }
  if (prev != nullptr) {
    if (prev->decompressed_offset + prev->decompressed_size != entry.decompressed_offset) {
      // Invariant I2
      FX_LOGS(ERROR) << "Invalid archive. Decompressed chunks are non-contiguous";
      return kStatusErrIoDataIntegrity;
    }
    if (prev->compressed_offset + prev->compressed_size > entry.compressed_offset) {
      // Invariant I3
      FX_LOGS(ERROR) << "Invalid archive. Chunks are non-monotonic";
      return kStatusErrIoDataIntegrity;
    }
  } else if (entry.decompressed_offset != 0) {
    // Invariant I0
    FX_LOGS(ERROR) << "Invalid archive. Decompressed chunks must start at offset 0";
    return kStatusErrIoDataIntegrity;
  }
  return kStatusOk;
}

uint32_t HeaderReader::ComputeChecksum(const uint8_t* header, size_t header_length) {
  constexpr size_t kOffsetAfterChecksum = kChunkArchiveHeaderCrc32Offset + sizeof(uint32_t);
  ZX_DEBUG_ASSERT(kOffsetAfterChecksum < header_length);

  // Independently compute a checksum for the bytes before and after the CRC32 slot, using the first
  // as a seed for the second to combine them.
  constexpr uint32_t seed = 0u;
  uint32_t first_crc = crc32(seed, header, kChunkArchiveHeaderCrc32Offset);
  uint32_t crc =
      crc32(first_crc, header + kOffsetAfterChecksum, header_length - kOffsetAfterChecksum);
  return crc;
}

// HeaderWriter

Status HeaderWriter::Create(void* dst, size_t dst_len, size_t num_frames, HeaderWriter* out) {
  if (num_frames > kChunkArchiveMaxFrames) {
    return kStatusErrInvalidArgs;
  } else if (dst_len < MetadataSizeForNumFrames(num_frames)) {
    return kStatusErrBufferTooSmall;
  }
  *out = HeaderWriter(dst, dst_len, num_frames);
  return kStatusOk;
}

HeaderWriter::HeaderWriter(void* dst, size_t dst_len, size_t num_frames)
    : dst_(static_cast<uint8_t*>(dst)) {
  ZX_DEBUG_ASSERT(num_frames <= kChunkArchiveMaxFrames);

  num_frames_ = static_cast<ChunkCountType>(num_frames);
  entries_ = reinterpret_cast<SeekTableEntry*>(dst_ + kChunkArchiveSeekTableOffset);

  dst_length_ = MetadataSizeForNumFrames(num_frames);
  ZX_DEBUG_ASSERT(dst_len >= dst_length_);
  bzero(dst_, dst_length_);
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
  memcpy(dst_, kChunkArchiveMagic, kArchiveMagicLength);
  reinterpret_cast<ArchiveVersionType*>(dst_ + kChunkArchiveVersionOffset)[0] = kVersion;
  reinterpret_cast<ChunkCountType*>(dst_ + kChunkArchiveNumChunksOffset)[0] = num_frames_;

  // Always compute checkum last.
  reinterpret_cast<uint32_t*>(dst_ + kChunkArchiveHeaderCrc32Offset)[0] =
      HeaderReader::ComputeChecksum(dst_, dst_length_);

  return kStatusOk;
}

}  // namespace chunked_compression
