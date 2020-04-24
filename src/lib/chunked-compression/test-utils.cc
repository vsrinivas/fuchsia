// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/cksum.h>
#include <zircon/assert.h>
#include <zircon/types.h>

#include <initializer_list>

#include <fbl/array.h>
#include <src/lib/chunked-compression/chunked-archive.h>

namespace chunked_compression {
namespace test_utils {

uint32_t ComputeChecksum(const uint8_t* header, size_t header_length) {
  constexpr size_t kOffsetAfterChecksum = kChunkArchiveHeaderCrc32Offset + sizeof(uint32_t);
  ZX_ASSERT(kOffsetAfterChecksum < header_length);
  ZX_ASSERT(kChunkArchiveMinHeaderSize <= header_length);

  // Independently compute a checksum for the bytes before and after the CRC32 slot, using the first
  // as a seed for the second to combine them.
  constexpr uint32_t seed = 0u;
  uint32_t first_crc = crc32(seed, header, kChunkArchiveHeaderCrc32Offset);
  return crc32(first_crc, header + kOffsetAfterChecksum, header_length - kOffsetAfterChecksum);
}

fbl::Array<uint8_t> CreateHeader(std::initializer_list<SeekTableEntry> entries) {
  unsigned num_entries = static_cast<unsigned>(entries.size());
  size_t sz = kChunkArchiveSeekTableOffset + (num_entries * sizeof(SeekTableEntry));
  fbl::Array<uint8_t> buf(new uint8_t[sz], sz);

  bzero(buf.get(), sz);

  // In practice the magic is always at the start of the header, but for consistency with other
  // accesses we offset |data| by |kChunkArchiveMagicOffset|.
  memcpy(buf.get(), kChunkArchiveMagic, kArchiveMagicLength);
  reinterpret_cast<ArchiveVersionType*>(buf.get() + kChunkArchiveVersionOffset)[0] = kVersion;
  reinterpret_cast<ChunkCountType*>(buf.get() + kChunkArchiveNumChunksOffset)[0] = num_entries;
  for (unsigned i = 0; i < num_entries; ++i) {
    reinterpret_cast<SeekTableEntry*>(buf.get() + kChunkArchiveSeekTableOffset)[i] =
        entries.begin()[i];
  }

  reinterpret_cast<uint32_t*>(buf.get() + kChunkArchiveHeaderCrc32Offset)[0] =
      ComputeChecksum(buf.get(), sz);

  return buf;
}

}  // namespace test_utils
}  // namespace chunked_compression
