// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/syslog/cpp/log_settings.h>
#include <lib/syslog/cpp/macros.h>
#include <stddef.h>
#include <stdint.h>

#include <vector>

#include <fbl/array.h>
#include <src/lib/chunked-compression/chunked-archive.h>
#include <src/lib/chunked-compression/chunked-decompressor.h>
#include <src/lib/chunked-compression/test-utils.h>

namespace {

using chunked_compression::ChunkCountType;
using chunked_compression::ChunkedDecompressor;
using chunked_compression::HeaderReader;
using chunked_compression::HeaderWriter;
using chunked_compression::kChunkArchiveHeaderCrc32Offset;
using chunked_compression::kChunkArchiveMinHeaderSize;
using chunked_compression::kChunkArchiveNumChunksOffset;
using chunked_compression::kStatusOk;
using chunked_compression::SeekTable;
using chunked_compression::SeekTableEntry;
using chunked_compression::Status;
using chunked_compression::test_utils::ComputeChecksum;

fbl::Array<uint8_t> CopyAndFixChecksum(const uint8_t *data, size_t size) {
  fbl::Array<uint8_t> data_copy(new uint8_t[size], size);
  memcpy(data_copy.get(), data, size);

  static_assert(chunked_compression::kVersion == 2u, "Update this fuzzer if the format changes");
  // Help guide the fuzzer by "fixing" the checksum.
  // Doing this requires knowing how large the archive ought to be, which is based on the
  // |num_chunks| field.
  ChunkCountType num_chunks =
      reinterpret_cast<ChunkCountType *>(data_copy.get() + kChunkArchiveNumChunksOffset)[0];

  size_t expected_header_size = HeaderWriter::MetadataSizeForNumFrames(num_chunks);
  uint32_t checksum;
  if (expected_header_size > size) {
    // Header is impossibly big, so don't try to compute the checksum.
    checksum = 0;
  } else {
    checksum = ComputeChecksum(data, expected_header_size);
  }
  reinterpret_cast<uint32_t *>(data_copy.get() + kChunkArchiveHeaderCrc32Offset)[0] = checksum;

  return data_copy;
}

}  // namespace

// Fuzz test which attempts to decompress |data| as a chunked archive.
extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  // Parse errors are logged at LOG_ERROR, so squelch them to avoid log spam.
  syslog::LogSettings settings;
  settings.min_log_level = syslog::LOG_FATAL;
  syslog::SetLogSettings(settings);

  if (size < kChunkArchiveMinHeaderSize) {
    return 0;
  }

  fbl::Array<uint8_t> data_copy = CopyAndFixChecksum(data, size);

  HeaderReader reader;
  SeekTable table;
  Status status = reader.Parse(data_copy.get(), size, size, &table);
  if (status != kStatusOk) {
    return 0;
  }
  size_t decompressed_size = table.DecompressedSize();
  if (decompressed_size > 1024 * 1024) {
    // Disallow >1MB decompressions, since this will most likely just fail to allocate.
    return 0;
  }

  ChunkedDecompressor decompressor;
  fbl::Array<uint8_t> output(new uint8_t[decompressed_size], decompressed_size);
  for (const SeekTableEntry &entry : table.Entries()) {
    // These should have been checked during parsing.
    ZX_ASSERT(entry.compressed_offset + entry.compressed_size <= size);
    ZX_ASSERT(entry.decompressed_offset + entry.decompressed_size <= decompressed_size);

    const uint8_t *input = data_copy.get() + entry.compressed_offset;
    size_t frame_sz = 0;
    decompressor.Decompress(table, input, entry.compressed_size, output.get(), output.size(),
                            &frame_sz);
    ZX_ASSERT(frame_sz <= decompressed_size);
  }

  return 0;
}
