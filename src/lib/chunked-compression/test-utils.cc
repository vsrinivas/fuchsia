// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zircon/assert.h>
#include <zircon/types.h>

#include <initializer_list>

#include <fbl/algorithm.h>
#include <fbl/array.h>
#include <src/lib/chunked-compression/chunked-archive.h>

namespace chunked_compression {

fbl::Array<uint8_t> CreateHeader(std::initializer_list<SeekTableEntry> entries) {
  unsigned num_entries = static_cast<unsigned>(entries.size());
  size_t sz = kChunkArchiveSeekTableOffset + (num_entries * sizeof(SeekTableEntry));
  fbl::Array<uint8_t> buf(new uint8_t[sz], sz);

  // In practice the magic is always at the start of the header, but for consistency with other
  // accesses we offset |data| by |kChunkArchiveMagicOffset|.
  reinterpret_cast<ArchiveMagicType*>(buf.get() + kChunkArchiveMagicOffset)[0] =
      kChunkedCompressionArchiveMagic;
  reinterpret_cast<ArchiveVersionType*>(buf.get() + kChunkArchiveVersionOffset)[0] = kVersion;
  reinterpret_cast<ChunkCountType*>(buf.get() + kChunkArchiveNumChunksOffset)[0] = num_entries;
  for (unsigned i = 0; i < num_entries; ++i) {
    reinterpret_cast<SeekTableEntry*>(buf.get() + kChunkArchiveSeekTableOffset)[i] =
        entries.begin()[i];
  }
  return buf;
}

}  // namespace chunked_compression
