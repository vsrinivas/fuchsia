// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_ZXDUMP_DUMP_FILE_H_
#define SRC_LIB_ZXDUMP_DUMP_FILE_H_

#include <lib/fitx/result.h>
#include <lib/zxdump/types.h>
#include <zircon/assert.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include <fbl/unique_fd.h>

#include "core.h"
#include "job-archive.h"

namespace zxdump::internal {

inline constexpr size_t kHeaderProbeSize = std::max(kMinimumElf, kMinimumArchive);

// The bounds of an archive member file inside the underlying real dump file.
// Member files inside nested archives have flat offsets into the real file.
struct FileRange {
  static constexpr FileRange Unbounded() { return {0, std::numeric_limits<size_t>::max()}; }

  bool empty() const { return size == 0; }

  // Subdivide this range by a subrange.  The given subrange must be valid with
  // this range as its base.  The returned range is a subrange relative to the
  // original base of this range, no earlier or larger than this range.
  FileRange operator/(FileRange subrange) const {
    ZX_DEBUG_ASSERT(subrange.offset <= size);
    ZX_DEBUG_ASSERT(size - subrange.offset >= subrange.size);
    subrange.offset += offset;
    return subrange;
  }

  FileRange& operator/=(FileRange subrange) {
    *this = *this / subrange;
    return *this;
  }

  FileRange operator/(size_t keep_prefix) const { return *this / FileRange{0, keep_prefix}; }

  FileRange& operator/=(size_t keep_prefix) {
    *this = *this / keep_prefix;
    return *this;
  }

  FileRange operator%(size_t remove_prefix) const {
    ZX_DEBUG_ASSERT(remove_prefix <= size);
    return *this / FileRange{remove_prefix, size - remove_prefix};
  }

  FileRange& operator%=(size_t remove_prefix) {
    *this = *this % remove_prefix;
    return *this;
  }

  uint64_t offset;
  uint64_t size;
};

// Each open dump file is one of these.
class DumpFile {
 public:
  virtual ~DumpFile();

  // Read a new dump file, using mmap if possible or else stdio.
  static fitx::result<Error, std::unique_ptr<DumpFile>> Open(fbl::unique_fd fd,
                                                             bool try_mmap = true);

  // Returns true if the probed bytes indicate a compressed file.  The buffer
  // is expected to be at least kHeaderProbeSize to be able to match anything.
  static bool IsCompressed(ByteView header);

  // Return a new DumpFile that decompresses part of this one by doing
  // ReadEphemeral calls on it.  The new DumpFile's lifetime must not exceed
  // this object's lifetime.  The underlying object should not be used for
  // ReadEphemeral while the decompressor object is being used.
  fitx::result<Error, std::unique_ptr<DumpFile>> Decompress(FileRange where, ByteView header);

  // Return the size of the file.  This may not be known for a streaming input,
  // in which case this value acts only as an upper bound.
  virtual size_t size() const = 0;

  size_t size_bytes() const { return size(); }

  // Reduce resources when no more reading will be done but the object is kept
  // alive for ReadPermanent results.
  virtual void shrink_to_fit() = 0;

  // Read a range of the file, yielding a pointer that's valid as long as this
  // object lives.  When not doing mmap, this has to copy it all in memory.
  virtual fitx::result<Error, ByteView> ReadPermanent(FileRange fr) = 0;

  // Read a range of the file, yielding a pointer that's only guaranteed to be
  // valid until the next ReadEphemeral (or ReadProbe) call on the same object.
  virtual fitx::result<Error, ByteView> ReadEphemeral(FileRange fr) = 0;

  // This does ReadEphemeral (and so it invalidates past ReadEphemeral results
  // and vice versa), but if the dump file ends before the whole range, just
  // return a shorter range rather than the "truncated dump" error.
  virtual fitx::result<Error, ByteView> ReadProbe(FileRange fr) = 0;

 private:
  // This is used by both Stdio and Zstd.
  using Buffer = std::vector<std::byte>;

  // These are the different implementation subclasses.
  class Stdio;
  class Mmap;
  class Zstd;
};

// Helpers for some common errors.

constexpr auto TruncatedDump() {
  return fitx::error(Error{
      "truncated dump",
      ZX_ERR_OUT_OF_RANGE,
  });
}

constexpr auto CorruptedDump() {
  return fitx::error(Error{
      "corrupted dump",
      ZX_ERR_IO_DATA_INTEGRITY,
  });
}

}  // namespace zxdump::internal

#endif  // SRC_LIB_ZXDUMP_DUMP_FILE_H_
