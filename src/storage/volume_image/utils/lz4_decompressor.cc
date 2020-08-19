// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/volume_image/utils/lz4_decompressor.h"

#include <lib/fit/result.h>

#include <cstddef>

#include "src/storage/volume_image/options.h"
#include "src/storage/volume_image/utils/lz4_result.h"

namespace storage::volume_image {
namespace {

// Zero initialized.
constexpr LZ4F_decompressOptions_t kDefaultOptions = {};

}  // namespace

fit::result<Lz4Decompressor, std::string> Lz4Decompressor::Create(
    const CompressionOptions &options) {
  if (options.schema != CompressionSchema::kLz4) {
    return fit::error("Lz4Compressor requires" + EnumAsString(CompressionSchema::kLz4) +
                      ". Provided: " + EnumAsString(options.schema) + ".");
  }

  return fit::ok(Lz4Decompressor());
}

Lz4Decompressor::~Lz4Decompressor() {
  if (context_ != nullptr) {
    LZ4F_freeDecompressionContext(context_);
  }
}

fit::result<void, std::string> Lz4Decompressor::Prepare(Handler handler) {
  if (state_ != State::kInitalized && state_ != State::kFinalized) {
    return fit::error(
        "Lz4Decompressor::Prepare may only be called in |State::kInitialized| or "
        "|State::kFinalized|state.");
  }

  if (handler == nullptr) {
    return fit::error("Lz4Decompressor::Prepare requires a valid |handler|.");
  }

  Lz4Result result = LZ4F_createDecompressionContext(&context_, LZ4F_VERSION);
  if (result.is_error()) {
    return fit::error("Failed to create LZ4 Compression Context. LZ4 Error: " +
                      std::string(result.error()) + ".");
  }

  // Adjust buffer so it fits the header.
  if (decompression_buffer_.size() < LZ4F_HEADER_SIZE_MAX) {
    decompression_buffer_.resize(LZ4F_HEADER_SIZE_MAX, 0);
  }

  handler_ = std::move(handler);
  state_ = State::kPrepared;

  return fit::ok();
}

fit::result<Decompressor::DecompressResult, std::string> Lz4Decompressor::Decompress(
    fbl::Span<const uint8_t> compressed_data) {
  if (state_ != State::kPrepared && state_ != State::kDecompressed) {
    return fit::error(
        "Lz4Decompressor::Decompress may only be called in |State::kPrepared| or "
        "|State::kDecompressed| state.");
  }

  size_t written_bytes = decompression_buffer_.size();
  size_t read_bytes = compressed_data.size();
  Lz4Result decompress_result =
      LZ4F_decompress(context_, decompression_buffer_.data(), &written_bytes,
                      compressed_data.data(), &read_bytes, &kDefaultOptions);
  if (decompress_result.is_error()) {
    return fit::error("Lz4Decompressor::Decompress failed. LZ4 Error: " +
                      std::string(decompress_result.error()) + ".");
  }

  auto handler_result =
      handler_(fbl::Span<const uint8_t>(decompression_buffer_.data(), written_bytes));
  if (handler_result.is_error()) {
    return handler_result.take_error_result();
  }
  state_ = State::kDecompressed;

  // Extend the buffer to hint size.
  if (decompress_result.byte_count() > decompression_buffer_.size()) {
    decompression_buffer_.resize(decompress_result.byte_count(), 0);
  }

  // lz4_decompress returns 0 when the end of the decompression frame has been reached.
  return fit::ok(
      DecompressResult{.hint = decompress_result.byte_count(), .read_bytes = read_bytes});
}

fit::result<void, std::string> Lz4Decompressor::Finalize() {
  if (state_ != State::kPrepared && state_ != State::kDecompressed) {
    return fit::error(
        "Lz4Decompressor::Decompress may only be called in |State::kPrepared| or "
        "|State::kDecompressed| state.");
  }

  Lz4Result result = LZ4F_freeDecompressionContext(context_);
  if (result.is_error()) {
    return fit::error(
        "Failed to free LZ4 Compression Context. LZ4 Error: " + std::string(result.error()) + ".");
  }
  context_ = nullptr;
  state_ = State::kFinalized;

  return fit::ok();
}

void Lz4Decompressor::ProvideSizeHint(size_t size_hint) {
  if (size_hint >= decompression_buffer_.size()) {
    decompression_buffer_.resize(size_hint, 0);
  }
}

}  // namespace storage::volume_image
