// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/zbitl/decompress.h>
#include <zircon/assert.h>

#include <functional>

#include <zstd/zstd.h>

namespace zbitl::decompress {

using namespace std::literals;

static_assert(kReadMinimum >= ZSTD_FRAMEHEADERSIZE_MAX);

size_t OneShot::GetScratchSize() {
  // zstd requires its buffer to be aligned to 8 bytes.
  static_assert(__STDCPP_DEFAULT_NEW_ALIGNMENT__ >= 8);
  return ZSTD_estimateDCtxSize();
}

OneShot::Context* OneShot::Init(void* scratch_space, size_t size) {
  return reinterpret_cast<Context*>(ZSTD_initStaticDCtx(scratch_space, size));
}

fit::result<std::string_view> OneShot::DecompressImpl(Context* ctx, cpp20::span<std::byte> out,
                                                      ByteView in) {
  // All-in-one mode.  This will be the only call made.
  auto dctx = reinterpret_cast<ZSTD_DCtx*>(ctx);
  size_t result = ZSTD_decompressDCtx(dctx, out.data(), out.size(), in.data(), in.size());
  if (ZSTD_isError(result)) {
    return fit::error{std::string_view{ZSTD_getErrorName(result)}};
  }
  ZX_ASSERT_MSG(result <= out.size(), "ZSTD_decompressDCtx wrote %zu into a buffer of %zu", result,
                out.size());
  if (result != out.size()) {
    return fit::error{"decompression produced too little data"sv};
  }
  return fit::ok();
}

}  // namespace zbitl::decompress
