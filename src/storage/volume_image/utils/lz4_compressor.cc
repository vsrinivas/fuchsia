// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/volume_image/utils/lz4_compressor.h"

#include "src/storage/volume_image/options.h"

namespace storage::volume_image {
namespace {

// Wrapper on top of LZ4* function return code.
class Lz4Result {
 public:
  // Implicit conversion from LZ4F_error_code_t.
  Lz4Result(LZ4F_errorCode_t code) : code_(code) {}

  // Returns true if the underlying |code_| is not an error.
  bool is_ok() const { return !is_error(); }

  // Returns true if the underlying |code_| is an error.
  bool is_error() const { return LZ4F_isError(code_); }

  // Returns a view into the error name of the underlying |code_|.
  std::string_view error() const {
    assert(is_error());
    return std::string_view(LZ4F_getErrorName(code_));
  }

  // Returns the byte count, when overriden return value happens. This usually means that
  // a function either returns a negative value or a number of bytes.
  size_t byte_count() const {
    assert(is_ok() && code_ >= 0);
    return static_cast<size_t>(code_);
  }

 private:
  LZ4F_errorCode_t code_ = -1;
};

Lz4Compressor::Preferences ConvertOptionsToPreferences(
    const CompressionOptions& compression_options) {
  Lz4Compressor::Preferences preferences = {};
  preferences.frameInfo.blockMode = LZ4F_blockIndependent;
  const auto& options = compression_options.options;

  auto const_it = options.find("block_size");
  int block_size_kb = 0;

  if (const_it != options.end()) {
    block_size_kb = static_cast<int>(const_it->second);
  }

  LZ4F_blockSizeID_t block_size_id = LZ4F_max64KB;
  if (block_size_kb <= 64) {
    block_size_id = LZ4F_max64KB;
  } else if (block_size_kb <= 256) {
    block_size_id = LZ4F_max256KB;
  } else if (block_size_kb <= 1024) {
    block_size_id = LZ4F_max1MB;
  } else {
    block_size_id = LZ4F_max4MB;
  }
  preferences.frameInfo.blockSizeID = block_size_id;

  const_it = options.find("compression_level");
  int compression_level = 0;

  if (const_it != options.end()) {
    compression_level = static_cast<int>(const_it->second);
  }
  preferences.compressionLevel = compression_level;

  return preferences;
}

}  // namespace

Lz4Compressor::~Lz4Compressor() {
  if (context_ != nullptr) {
    LZ4F_freeCompressionContext(context_);
    context_ = nullptr;
  }
}

fit::result<Lz4Compressor, std::string> Lz4Compressor::Create(const CompressionOptions& options) {
  if (options.schema != CompressionSchema::kLz4) {
    std::string error = "Lz4Compressor requires";
    error.append(EnumAsString(CompressionSchema::kLz4))
        .append(". Provided: ")
        .append(EnumAsString(options.schema))
        .append(".");
    return fit::error(error);
  }
  Preferences preferences = ConvertOptionsToPreferences(options);
  return fit::ok(Lz4Compressor(preferences));
}

fit::result<void, std::string> Lz4Compressor::Prepare(Handler handler) {
  if (state_ != State::kInitalized && state_ != State::kFinalized) {
    return fit::error("Lz4Compressor::Prepare must be in |kInitialized| or |kFinalized| state.");
  }

  if (handler == nullptr) {
    return fit::error("Lz4Compressor::Prepare requires a valid |handler|.");
  }

  Lz4Result result = LZ4F_createCompressionContext(&context_, LZ4F_VERSION);
  if (result.is_error()) {
    std::string error = "Failed to create LZ4 Compression Context. LZ4 Error: ";
    error.append(result.error()).append(".");
    return fit::error(error);
  }

  // Adjust buffer so it fits the header.
  if (compression_buffer_.size() < LZ4F_HEADER_SIZE_MAX) {
    compression_buffer_.resize(LZ4F_HEADER_SIZE_MAX, 0);
  }

  handler_ = std::move(handler);
  result = LZ4F_compressBegin(context_, compression_buffer_.data(), compression_buffer_.size(),
                              &preferences_);
  if (result.is_error()) {
    std::string error = "Failed to emit LZ4 Frame header. LZ4 Error: ";
    error.append(result.error()).append(".");
    return fit::error(error);
  }
  state_ = State::kPrepared;

  return handler_(fbl::Span(compression_buffer_.data(), result.byte_count()));
}

fit::result<void, std::string> Lz4Compressor::Compress(fbl::Span<const uint8_t> uncompressed_data) {
  if (state_ != State::kPrepared && state_ != State::kCompressed) {
    return fit::error("Lz4Compressor::Compress must be in |kPrepared| or |kCompressed| state.");
  }

  size_t max_compressed_size = LZ4F_compressBound(uncompressed_data.size(), &preferences_);
  if (compression_buffer_.size() < max_compressed_size) {
    compression_buffer_.resize(max_compressed_size, 0);
  }

  Lz4Result result =
      LZ4F_compressUpdate(context_, compression_buffer_.data(), compression_buffer_.size(),
                          uncompressed_data.data(), uncompressed_data.size(), nullptr);
  if (result.is_error()) {
    std::string error = "Failed to compress data with LZ4 compressor. LZ4 Error: ";
    error.append(result.error()).append(".");
    return fit::error(error);
  }
  state_ = State::kCompressed;
  return handler_(fbl::Span(compression_buffer_.data(), result.byte_count()));
}

fit::result<void, std::string> Lz4Compressor::Finalize() {
  if (state_ != State::kCompressed) {
    return fit::error("Lz4Compressor::Finalize must be in |kCompressed| state.");
  }

  size_t max_compressed_size = LZ4F_compressBound(0, &preferences_);
  if (compression_buffer_.size() < max_compressed_size) {
    compression_buffer_.resize(max_compressed_size, 0);
  }

  Lz4Result result =
      LZ4F_compressEnd(context_, compression_buffer_.data(), compression_buffer_.size(), nullptr);
  if (result.is_error()) {
    std::string error = "Failed to finalize compression with LZ4 Compressor. LZ4 Error: ";
    error.append(result.error()).append(".");
    return fit::error(error);
  }
  auto handler_result = handler_(fbl::Span(compression_buffer_.data(), result.byte_count()));

  // Even though we can reuse compression context after compressionEnd, its preferred not to
  // delegate this to the destructor, since it may error, and we wont be able to surface it.
  result = LZ4F_freeCompressionContext(context_);
  context_ = nullptr;
  if (result.is_error()) {
    std::string error = "Failed to free compression contrext in LZ4 Compressor. LZ4 Error: ";
    error.append(result.error()).append(".");
    return fit::error(error);
  }

  state_ = State::kFinalized;
  return handler_result;
}

}  // namespace storage::volume_image
