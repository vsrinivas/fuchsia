// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_VOLUME_IMAGE_UTILS_COMPRESSOR_H_
#define SRC_STORAGE_VOLUME_IMAGE_UTILS_COMPRESSOR_H_

#include <lib/fit/function.h>
#include <lib/fpromise/result.h>
#include <lib/stdcompat/span.h>

#include <string>

namespace storage::volume_image {

// This interface represents a compressor state machine.
//
//  Prepare -> Compress -+-+-> Finalize -+-+-> End
//      ^          ^     |                |
//      |          |+-+-+|                |
//      |-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-|
//
//  In order to compress independent uncompressed data blocks, the user
//  must go through |Finalize| -> |Prepare| state transition, allowing for new,
//  independent data to be compressed.
//
//  In order to decompress the output of a compressor, the symbols generated
//  by the compressor must be fed in sequential order to the decompressor.
//
// This class is thread-unsafe, since there might be buffered data from previous calls.
class Compressor {
 public:
  // A Handler is the interface used to make the compressed data available to the caller,
  // for processing. Anytime a compressor emits symbols, it will call the provided |Handler|.
  //
  // The compressed data is only guaranteed to be valid within a single call to the handler.
  using Handler = fit::function<fpromise::result<void, std::string>(cpp20::span<const uint8_t>)>;

  virtual ~Compressor() = default;

  // Returns |fpromise::ok| on success. Setting |handler| for consuming symbols emitted during
  // compression.
  //
  // On failure, returns a string decribing the error condition.
  virtual fpromise::result<void, std::string> Prepare(Handler handler) = 0;

  // Returns |fpromise::ok| on success.
  //
  // On failure, returns a string decribing the error condition.
  virtual fpromise::result<void, std::string> Compress(
      cpp20::span<const uint8_t> uncompressed_data) = 0;

  // Returns |fpromise::ok| on success. At this point all remaining symbols for the compressed
  // representation will be emitted.
  //
  // On failure, returns a string describing the error condition.
  virtual fpromise::result<void, std::string> Finalize() = 0;
};

}  // namespace storage::volume_image

#endif  // SRC_STORAGE_VOLUME_IMAGE_UTILS_COMPRESSOR_H_
