// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_CHUNKED_COMPRESSION_CHUNKED_ARCHIVE_H_
#define SRC_LIB_CHUNKED_COMPRESSION_CHUNKED_ARCHIVE_H_

#include <optional>

#include <fbl/algorithm.h>
#include <fbl/array.h>
#include <fbl/macros.h>

#include "status.h"

// Format specification for chunked archives.
//
// A chunked archive has a Header followed by zero or more Frames.
//
// ## Header
//
// The header describes the format of the archive and contains the Seek Table which maps the
// compressed frames to decompressed space.
//
// This header describes *Version 2* of the format. All other versions are unsupported.
//
//       0     1     2     3     4     5     6     7
//    +-----+-----+-----+-----+-----+-----+-----+-----+
//  0 |                 Magic Number                  |
//    +-----+-----+-----+-----+-----+-----+-----+-----+
//  8 |  Version  |  Reserved |       Num Frames      |  // Reserved bytes must be zero.
//    +-----+-----+-----+-----+-----+-----+-----+-----+
// 16 |    Header CRC32       |        Reserved       |  // Reserved bytes must be zero.
//    +-----+-----+-----+-----+-----+-----+-----+-----+
// 24 |                    Reserved                   |  // Reserved bytes must be zero.
//    +-----+-----+-----+-----+-----+-----+-----+-----+
// 32 |                                               |
// 40 |                   Seek Table                  |
// 48 |                     Entry                     |
// 56 |                                               |
//    +-----+-----+-----+-----+-----+-----+-----+-----+
// .. |                                               |
// .. |                   Seek Table                  |
// .. |                     Entry                     |
// .. |                                               |
//    +-----+-----+-----+-----+-----+-----+-----+-----+
//
// The Header CRC32 is computed based on the entire header including each Seek Table Entry.
//
// ### Seek Table
//
// Each Seek Table Entry describes a contiguous range of data in the compressed space, and where
// in the decompressed data it expands to.
//
//    +-----+-----+-----+-----+-----+-----+-----+-----+
//  0 |               Decompressed Offset             |
//    +-----+-----+-----+-----+-----+-----+-----+-----+
//  8 |                Decompressed Size              |
//    +-----+-----+-----+-----+-----+-----+-----+-----+
// 16 |                Compressed Offset              |
//    +-----+-----+-----+-----+-----+-----+-----+-----+
// 24 |                 Compressed Size               |
//    +-----+-----+-----+-----+-----+-----+-----+-----+
//
// Seek table entries are *contiguous* in decompressed space, but may be *discontiguous* in
// compressed space. This is to support adding alignment/padding to output files to improve storage
// access efficiency.
//
// A seek table can hold at most 1023 entries (which results in a 32KiB header).
//
// ### Seek Table Invariants
//
// I0: The first seek table entry must have decompressed offset 0.
// I1: The first seek table entry must have compressed offset greater than or equal to the size of
//     the header.
// I2: Each entry's decompressed offset must be equal to the end of the previous frame (i.e. to
//     the previous frame's decompressed offset+length).
// I3: Each entry's compressed offset must be greater than or equal to the end of the previous
//     frame (i.e. to the previous frame's compressed offset+length).
// I4: Each entry must have a non-zero decompressed and compressed length.
// I5: No compressed frame may exceed the end of the file.
//
// ## Compressed Frames
//
// The compressed frames are contiguous ranges of bytes stored in the file at the offsets described
// by their seek table entry.
//
// Any ranges of bytes in the file not covered by the seek table are ignored.

namespace chunked_compression {

using ArchiveVersionType = uint16_t;
using ChunkCountType = uint32_t;

// The magic number is an arbitrary unique value used to identify files as being of this format.  It
// can be derived as follows:
//
//   sha256sum <<< "Fuchsia is a vivid purplish red color" | head -c16
//
constexpr size_t kArchiveMagicLength = sizeof(uint64_t);
constexpr uint8_t kChunkArchiveMagic[kArchiveMagicLength] = {
    0x46, 0x9b, 0x78, 0xef, 0x0f, 0xd0, 0xb2, 0x03,
};
constexpr ArchiveVersionType kVersion = 2u;

constexpr size_t kChunkArchiveMagicOffset = 0ul;
constexpr size_t kChunkArchiveVersionOffset = 8ul;
constexpr size_t kChunkArchiveReserved1Offset = 10ul;
constexpr size_t kChunkArchiveNumChunksOffset = 12ul;
constexpr size_t kChunkArchiveHeaderCrc32Offset = 16ul;
constexpr size_t kChunkArchiveReserved2Offset = 20ul;
constexpr size_t kChunkArchiveSeekTableOffset = 32ul;

// A single entry into the seek table. Describes where an extent of decompressed
// data lives in the compressed space.
struct SeekTableEntry {
  uint64_t decompressed_offset;
  uint64_t decompressed_size;
  uint64_t compressed_offset;
  uint64_t compressed_size;
};
static_assert(sizeof(SeekTableEntry) == 32ul, "Breaking change to archive format");

constexpr ChunkCountType kChunkArchiveMaxFrames = 1023;

constexpr size_t kChunkArchiveMinHeaderSize = kChunkArchiveSeekTableOffset;
constexpr size_t kChunkArchiveMaxHeaderSize =
    kChunkArchiveSeekTableOffset + (kChunkArchiveMaxFrames * sizeof(SeekTableEntry));

// This assert just documents the relationship between the maximum number of frames and the actual
// maximum header size (32KiB).
static_assert(kChunkArchiveMaxHeaderSize == (32 * 1024), "");

static_assert(kChunkArchiveMagicOffset == 0ul, "Breaking change to archive format");
static_assert(kChunkArchiveVersionOffset == kChunkArchiveMagicOffset + kArchiveMagicLength,
              "Breaking change to archive format");
static_assert(kChunkArchiveReserved1Offset ==
                  kChunkArchiveVersionOffset + sizeof(ArchiveVersionType),
              "Breaking change to archive format");
static_assert(kChunkArchiveNumChunksOffset == kChunkArchiveReserved1Offset + sizeof(uint16_t),
              "Breaking change to archive format");
static_assert(kChunkArchiveHeaderCrc32Offset ==
                  kChunkArchiveNumChunksOffset + sizeof(ChunkCountType),
              "Breaking change to archive format");
static_assert(kChunkArchiveReserved2Offset == kChunkArchiveHeaderCrc32Offset + sizeof(uint32_t),
              "Breaking change to archive format");
static_assert(kChunkArchiveSeekTableOffset ==
                  kChunkArchiveReserved2Offset + sizeof(uint32_t) + sizeof(uint64_t),
              "Breaking change to archive format");

// A parsed view of a chunked archive's seek table.
// Constructed by parsing a buffer containing a raw archive (see |HeaderReader::Parse()|).
class SeekTable {
 public:
  SeekTable() = default;
  SeekTable(SeekTable&& o) = default;
  SeekTable& operator=(SeekTable&& o) = default;
  ~SeekTable() = default;
  DISALLOW_COPY_AND_ASSIGN_ALLOW_MOVE(SeekTable);

  // Returns a reference to the seek table of the archive.
  const fbl::Array<SeekTableEntry>& Entries() const { return entries_; }

  // Returns the size of the compressed archive.
  // Equal to the end of the greatest frame (i.e. its offset + size).
  size_t CompressedSize() const;

  // Returns the size of the serialized header (i.e. everything but the actual compressed frames).
  size_t SerializedHeaderSize() const;

  // Returns the expected size of the archive after decompression.
  // Equal to the end of the greatest frame (i.e. its offset + size).
  size_t DecompressedSize() const;

  // Lookup functions to find the entry in the seek table which covers |offset| in either the
  // compressed or decompressed space.
  // Returns the index into |SeekTable()| where the entry is stored, or std::nullopt if the
  // offset is not covered. (Note that there can be gaps in the *compressed* frames, but the
  // decompressed frames are contiguous).
  std::optional<unsigned> EntryForCompressedOffset(size_t offset) const;
  std::optional<unsigned> EntryForDecompressedOffset(size_t offset) const;

  // Allow HeaderReader to initialize these objects with a validated parsed seek table
  friend class HeaderReader;

 private:
  fbl::Array<SeekTableEntry> entries_;
};

// HeaderReader reads chunked archive headers and produces in-memory SeekTable representations.
class HeaderReader {
 public:
  HeaderReader() = default;
  DISALLOW_COPY_ASSIGN_AND_MOVE(HeaderReader);

  // Validates that |data| is a valid chunked archive header and fills |out| with a copy of its
  // seek table.
  // |len| must be at least long enough to include the entire header; any actual compressed frames
  // contained in |data| will not be accessed.
  // |file_length| is the known length of the overall file. This is used for sanity checking the
  // entries in the seek table. If any compressed frames exceed this length, the header is assumed
  // to be corrupted.
  Status Parse(const void* data, size_t len, size_t file_length, SeekTable* out);

  // Share validation routines with HeaderWriter
  friend class HeaderWriter;

 private:
  static Status CheckMagic(const uint8_t* data, size_t len);
  static Status CheckVersion(const uint8_t* data, size_t len);
  static Status CheckChecksum(const uint8_t* data, size_t len);
  static Status GetNumChunks(const uint8_t* data, size_t len, ChunkCountType* num_chunks_out);
  static Status ParseSeekTable(const uint8_t* data, size_t len, size_t file_length,
                               fbl::Array<SeekTableEntry>* entries_out);
  static Status CheckSeekTable(const fbl::Array<SeekTableEntry>& seek_table, size_t header_end,
                               size_t file_length);
  // NOTE: |prev| is optional, and is nullptr if |entry| is the first entry.
  // (Ideally this would be a std::optional<const T&>, but optional references are not legal.)
  static Status CheckSeekTableEntry(const SeekTableEntry& entry, const SeekTableEntry* prev,
                                    size_t header_end, size_t file_length);

  // Computes the CRC32 checksum for |header|.
  static uint32_t ComputeChecksum(const uint8_t* header, size_t header_length);
};

// HeaderWriter writes chunked archive headers to a target buffer.
class HeaderWriter {
 public:
  HeaderWriter() = default;
  HeaderWriter(HeaderWriter&& o) = default;
  HeaderWriter& operator=(HeaderWriter&& o) = default;
  DISALLOW_COPY_AND_ASSIGN_ALLOW_MOVE(HeaderWriter);

  // Initializes |out| to write a header to |dst| (which is a buffer of |dst_len| bytes).
  // Exactly |num_frames| will be written.
  static Status Create(void* dst, size_t dst_len, size_t num_frames, HeaderWriter* out);

  // Computes the number of frames which will be used to compress a |size|-byte input.
  static size_t NumFramesForDataSize(size_t size, size_t chunk_size) {
    return fbl::round_up(size, chunk_size) / chunk_size;
  }

  // Computes the size of the header for an archive with |num_frames|.
  static size_t MetadataSizeForNumFrames(size_t num_frames) {
    return kChunkArchiveSeekTableOffset + (num_frames * sizeof(SeekTableEntry));
  }

  // Adds a copy of |entry| to the seek table.
  // Returns an error if |entry| is invalid, overlaps an existing entry, or if the table is already
  // full.
  Status AddEntry(const SeekTableEntry& entry);

  // Finishes writing the header out to the target buffer.
  //
  // Returns an error if the header was not fully initialized (i.e. not every seek table entry
  // was filled).
  //
  // The target buffer is in an undefined state before Finalize() is called, and should not be
  // serialized until Finalize() returns successfully.
  //
  // The HeaderWriter is in an undefined state after Finalize() returns, regardless of
  // whether Finalize() succeeded or not.
  Status Finalize();

 private:
  HeaderWriter(void* dst, size_t dst_len, size_t num_frames);

  uint8_t* dst_ = nullptr;
  size_t dst_length_;
  SeekTableEntry* entries_ = nullptr;
  unsigned current_frame_ = 0;
  ChunkCountType num_frames_;
};

}  // namespace chunked_compression

#endif  // SRC_LIB_CHUNKED_COMPRESSION_CHUNKED_ARCHIVE_H_
