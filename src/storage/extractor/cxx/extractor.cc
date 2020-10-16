// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/extractor/c/extractor.h"

#include <lib/zx/status.h>
#include <zircon/errors.h>

#include <memory>

#include <fbl/unique_fd.h>
#include <safemath/checked_math.h>

#include "src/storage/extractor/cxx/extractor.h"

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

zx::status<> map_error(CResult result) {
  if (result.ok) {
    return zx::ok();
  }
  return zx::error(map_kind_to_err(result.kind));
}

}  // namespace

zx::status<std::unique_ptr<Extractor>> Extractor::Create(fbl::unique_fd input_stream,
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

zx::status<> Extractor::Add(uint64_t offset, uint64_t size, ExtentProperties properties) {
  return map_error(extractor_add(extractor_, offset, size, properties));
}

zx ::status<> Extractor::AddBlocks(uint64_t block_offset, uint64_t block_count,
                                   ExtentProperties properties) {
  return Add(safemath::CheckMul(block_offset, options_.alignment).ValueOrDie(),
             safemath::CheckMul(block_count, options_.alignment).ValueOrDie(), properties);
}

zx::status<> Extractor::AddBlock(uint64_t block_offset, ExtentProperties properties) {
  return AddBlocks(block_offset, 1, properties);
}

zx::status<> Extractor::Write() { return map_error(extractor_write(extractor_)); }

}  // namespace extractor
