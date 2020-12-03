// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/zbitl/decompress.h>
#include <zircon/assert.h>

#include <zstd/zstd.h>

namespace zbitl::decompress {

using namespace std::literals;

static_assert(kReadMinimum >= ZSTD_FRAMEHEADERSIZE_MAX);

fitx::result<std::string_view, Streaming::ScratchSize> Streaming::GetScratchSize(ByteView probe) {
  size_t size = ZSTD_estimateDStreamSize_fromFrame(probe.data(), probe.size());
  if (ZSTD_isError(size)) {
    return fitx::error{std::string_view{ZSTD_getErrorName(size)}};
  }
  // zstd requires its buffer to be aligned to 8 bytes.
  static_assert(__STDCPP_DEFAULT_NEW_ALIGNMENT__ >= 8);
  return fitx::ok(ScratchSize{size, ZSTD_BLOCKSIZE_MAX});
}

Streaming::Context* Streaming::Init(void* scratch_space, size_t size) {
  return reinterpret_cast<Context*>(ZSTD_initStaticDStream(scratch_space, size));
}

fitx::result<std::string_view, fbl::Span<std::byte>> Streaming::Decompress(
    Streaming::Context* dctx, fbl::Span<std::byte> buffer, ByteView& chunk) {
  // Streaming mode.  This may be one of many calls with consecutive chunks.

  auto stream = reinterpret_cast<ZSTD_DStream*>(dctx);
  ZSTD_inBuffer in = {chunk.data(), chunk.size(), 0};
  ZSTD_outBuffer out = {buffer.data(), buffer.size(), 0};

  while (in.pos < in.size && out.pos < out.size) {
    size_t result = ZSTD_decompressStream(stream, &out, &in);
    ZX_DEBUG_ASSERT_MSG(out.pos <= out.size, "ZSTD_decompressStream wrote %zu into a buffer of %zu",
                        out.pos, out.size);
    if (ZSTD_isError(result)) {
      return fitx::error{std::string_view{ZSTD_getErrorName(result)}};
    }
    if (result == 0) {
      // Finished all the input and flushed all the output.
      ZX_ASSERT(in.pos == in.size);
      break;
    }
  }

  ZX_DEBUG_ASSERT(in.pos <= chunk.size());
  chunk.remove_prefix(in.pos);

  ZX_DEBUG_ASSERT(out.pos <= buffer.size());
  buffer = {buffer.data() + out.pos, buffer.size() - out.pos};

  return fitx::ok(buffer);
}

}  // namespace zbitl::decompress
