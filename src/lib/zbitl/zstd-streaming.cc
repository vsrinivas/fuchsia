// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/zbitl/decompress.h>
#include <zircon/assert.h>

#include <zstd/zstd.h>

namespace zbitl::decompress {

using namespace std::literals;

static_assert(kReadMinimum >= ZSTD_FRAMEHEADERSIZE_MAX);

fit::result<std::string_view, Streaming::ScratchSize> Streaming::GetScratchSize(ByteView probe) {
  size_t size = ZSTD_estimateDStreamSize_fromFrame(probe.data(), probe.size());
  if (ZSTD_isError(size)) {
    return fit::error{std::string_view{ZSTD_getErrorName(size)}};
  }
  // zstd requires its buffer to be aligned to 8 bytes.
  static_assert(__STDCPP_DEFAULT_NEW_ALIGNMENT__ >= 8);
  return fit::ok(ScratchSize{size, ZSTD_BLOCKSIZE_MAX});
}

Streaming::Context* Streaming::Init(void* scratch_space, size_t size) {
  return reinterpret_cast<Context*>(ZSTD_initStaticDStream(scratch_space, size));
}

fit::result<std::string_view, cpp20::span<std::byte>> Streaming::Decompress(
    Streaming::Context* dctx, cpp20::span<std::byte> buffer, ByteView& chunk) {
  // Streaming mode.  This may be one of many calls with consecutive chunks.

  auto stream = reinterpret_cast<ZSTD_DStream*>(dctx);
  ZSTD_inBuffer in = {chunk.data(), chunk.size(), 0};
  ZSTD_outBuffer out = {buffer.data(), buffer.size(), 0};

  // Run the decompressor once before checking bounds, to ensure that a bounds
  // failure deriving from a spent buffer is reported and as we might consume
  // more of the chunk (i.e., with metadata bytes) without advancing `out.pos`.
  do {
    size_t result = ZSTD_decompressStream(stream, &out, &in);
    ZX_DEBUG_ASSERT_MSG(out.pos <= out.size, "ZSTD_decompressStream wrote %zu into a buffer of %zu",
                        out.pos, out.size);
    if (ZSTD_isError(result)) {
      return fit::error{std::string_view{ZSTD_getErrorName(result)}};
    }

    // Finished decompressing and flushed all the output.
    if (result == 0) {
      ZX_ASSERT(in.pos <= in.size);
      // While it is reasonable for ZSTD_decompressStream() to allow for midway
      // end-of-stream indicators, the presence here in a payload indicates bad
      // or corrupted data.
      if (in.pos != in.size) {
        return fit::error{"bad or corrupted data: end-of-stream indicator found too soon"};
      }
      break;
    }
  } while (in.pos < in.size && out.pos < out.size);

  ZX_DEBUG_ASSERT(in.pos <= chunk.size());
  chunk = chunk.subspan(in.pos);

  ZX_DEBUG_ASSERT(out.pos <= buffer.size());
  buffer = {buffer.data() + out.pos, buffer.size() - out.pos};

  return fit::ok(buffer);
}

}  // namespace zbitl::decompress
