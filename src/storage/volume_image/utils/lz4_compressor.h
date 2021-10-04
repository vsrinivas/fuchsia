// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_VOLUME_IMAGE_UTILS_LZ4_COMPRESSOR_H_
#define SRC_STORAGE_VOLUME_IMAGE_UTILS_LZ4_COMPRESSOR_H_

#include <lib/fpromise/result.h>
#include <lib/stdcompat/span.h>

#include <string>
#include <vector>

#include <lz4/lz4frame.h>

#include "src/storage/volume_image/options.h"
#include "src/storage/volume_image/utils/compressor.h"

namespace storage::volume_image {
// This class provides an implementation of |Compressor| backed by LZ4 Compression algorithm.
//
// This class is move construcable only.
class Lz4Compressor final : public Compressor {
 public:
  using Preferences = LZ4F_preferences_t;

  // Returns a |Lz4Compressor| on success.
  //
  // On failure, returns a string describing the error.
  static fpromise::result<Lz4Compressor, std::string> Create(const CompressionOptions& options);

  Lz4Compressor();
  explicit Lz4Compressor(const Preferences& preferences) : preferences_(preferences) {}
  Lz4Compressor(const Lz4Compressor&) = delete;
  Lz4Compressor(Lz4Compressor&&) noexcept = default;
  Lz4Compressor& operator=(const Lz4Compressor&) = delete;
  Lz4Compressor& operator=(Lz4Compressor&&) = delete;
  ~Lz4Compressor() final;

  // Returns |fpromise::ok| on success, allocating the necessary structures for repeated |Compress|
  // calls, and returning a buffer containing the header for the compressed data.
  //
  // On failure, returns a string decribing the error condition.
  fpromise::result<void, std::string> Prepare(Handler handler) final;

  // Returns |fpromise::ok| on success, returning a buffer with the compressed data of
  // |uncompressed_data|. The returned buffer, is only valid until the next call to |Compress|,
  // since contents may be overwritten.
  //
  // If the returned buffer is empty, data has been buffered, and symbols will be
  // emitted as next call to |Compress|, or when |Finalize| is called.
  //
  // On failure, returns a string decribing the error condition.
  fpromise::result<void, std::string> Compress(cpp20::span<const uint8_t> uncompressed_data) final;

  // Returns |fpromise::ok| on success, returning a buffer containing the symbols of any buffered
  // data not emitted on last |Compress| call, and set of symbols marking the end of the
  // compression. This call will free any allocated resources.
  //
  // On failure, returns a string describing the error condition.
  fpromise::result<void, std::string> Finalize() final;

  // Returns the set of preferences used for the underlying LZ4 compression.
  const Preferences& GetPreferences() const { return preferences_; }

 private:
  // Describes the possible states of the compressor.
  enum class State {
    // The Compressor was created with valid options, yet it has not been prepared.
    kInitalized,
    // The compressor, has been prepared, and is ready for compressing data.
    kPrepared,
    // The compressor has at least compressed a piece of data.
    kCompressed,
    // The compressor finished compressing, and has deallocated the required structures.
    kFinalized,
  };

  // LZ4 preferences used for the compressor.
  Preferences preferences_;

  // LZ$ compression context, that handles the LZ4 internals.
  LZ4F_compressionContext_t context_ = nullptr;

  // Current state of the compressor.
  State state_ = State::kInitalized;

  // Internal buffer used for storing decompressed data.
  std::vector<uint8_t> compression_buffer_;

  // Provides a callable for handling compressed representation symbols.
  Handler handler_;
};

}  // namespace storage::volume_image

#endif  // SRC_STORAGE_VOLUME_IMAGE_UTILS_LZ4_COMPRESSOR_H_
