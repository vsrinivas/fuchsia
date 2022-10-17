// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/extractor/c/extractor.h"

#include <lib/zx/status.h>
#include <zircon/errors.h>

#include <memory>

#include <fbl/unique_fd.h>
#include <safemath/checked_math.h>

#include "src/storage/extractor/cpp/extractor.h"

namespace extractor {

namespace {
zx_status_t map_kind_to_err(Error kind) {
  switch (kind) {
    case Error::CannotOverride:
      return ZX_ERR_ADDRESS_IN_USE;
    case Error::Exists:
      return ZX_ERR_ALREADY_EXISTS;
    case Error::NotAllowed:
      return ZX_ERR_NOT_SUPPORTED;
    case Error::SeekFailed:
    case Error::ReadFailed:
    case Error::WriteFailed:
      return ZX_ERR_IO;
    case Error::InvalidRange:
      return ZX_ERR_OUT_OF_RANGE;
    case Error::InvalidDataLength:
    case Error::InvalidOffset:
      return ZX_ERR_ADDRESS_UNREACHABLE;
    case Error::InvalidArgument:
      return ZX_ERR_INVALID_ARGS;
    case Error::ParseFailed:
      return ZX_ERR_BAD_STATE;
  }
}

zx::result<> map_error(CResult result) {
  if (result.ok) {
    return zx::ok();
  }
  return zx::error(map_kind_to_err(result.kind));
}

}  // namespace

zx::result<std::unique_ptr<Extractor>> Extractor::Create(fbl::unique_fd input_stream,
                                                         ExtractorOptions options,
                                                         fbl::unique_fd output_stream) {
  std::unique_ptr<Extractor> out(new Extractor());
  out->input_stream_ = std::move(input_stream);
  out->output_stream_ = std::move(output_stream);
  out->options_ = options;
  auto result =
      extractor_new(out->input_stream_.get(), options, out->output_stream_.get(), &out->extractor_);
  if (!result.ok) {
    return zx::error(map_kind_to_err(result.kind));
  }

  if (out->extractor_ == nullptr) {
    return zx::error(ZX_ERR_INVALID_ARGS);
  }
  return zx::ok(std::move(out));
}

zx::result<> Extractor::Add(uint64_t offset, uint64_t size, ExtentProperties properties) {
  return map_error(extractor_add(extractor_, offset, size, properties));
}

zx::result<> Extractor::AddBlocks(uint64_t block_offset, uint64_t block_count,
                                  ExtentProperties properties) {
  return Add(safemath::CheckMul(block_offset, options_.alignment).ValueOrDie(),
             safemath::CheckMul(block_count, options_.alignment).ValueOrDie(), properties);
}

zx::result<> Extractor::AddBlock(uint64_t block_offset, ExtentProperties properties) {
  return AddBlocks(block_offset, 1, properties);
}

zx::result<> Extractor::Write() { return map_error(extractor_write(extractor_)); }

zx::result<> Extractor::Deflate(fbl::unique_fd input_stream, fbl::unique_fd output_stream,
                                fbl::unique_fd verbose_stream) {
  return map_error(
      extractor_deflate(input_stream.get(), output_stream.get(), verbose_stream.get()));
}

}  // namespace extractor
