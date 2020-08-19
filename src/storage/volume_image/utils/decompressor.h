// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_VOLUME_IMAGE_UTILS_DECOMPRESSOR_H_
#define SRC_STORAGE_VOLUME_IMAGE_UTILS_DECOMPRESSOR_H_

#include <lib/fit/function.h>
#include <lib/fit/result.h>

#include <string>

#include <fbl/span.h>

namespace storage::volume_image {

// This interface represents a compressor state machine.
//
//  Prepare -> Decompress -+-+-> Finalize -+-+-> End
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
class Decompressor {
 public:
  // A Handler is the interface used to make the decompressed data available to the caller,
  // for processing. Anytime a decompressor emits symbols, it will call the provided |Handler|.
  //
  // The decompressed data is only guaranteed to be valid within a single call to the handler.
  // The decompressed data is not required to consumed the entire input, since symbols may have been
  // sliced.
  //
  // The decompressor will provide the handler with the number of consumed bytes if any.
  // The decompressor wil not provide the handler with consumbed byte count if the decompressions is
  // completed.
  using Handler =
      fit::function<fit::result<void, std::string>(fbl::Span<const uint8_t> decompressed_data)>;

  struct DecompressResult {
    // Zero if decompression is finished, otherwise provides a hint with respect to the size of the
    // decompressed data on next call to |Decompress|.
    size_t hint = 0;

    // Number of consumed bytes from input compressed data as a result of a call to |Decompress|.
    size_t read_bytes = 0;
  };

  virtual ~Decompressor() = default;

  // Returns |fit::ok| on success. Setting |handler| for consuming symbols emitted during
  // decompression.
  //
  // On failure, returns a string decribing the error condition.
  virtual fit::result<void, std::string> Prepare(Handler handler) = 0;

  // Returns |fit::ok| on success. When data has been fully decompressed, will return a
  // |DecompressResult| instance.
  //
  // On failure, returns a string decribing the error condition.
  virtual fit::result<DecompressResult, std::string> Decompress(
      fbl::Span<const uint8_t> compressed_data) = 0;

  // Returns |fit::ok| on success. At this point all remaining symbols for the decompressed
  // representation will be emitted.
  //
  // On failure, returns a string describing the error condition.
  virtual fit::result<void, std::string> Finalize() = 0;
};

}  // namespace storage::volume_image

#endif  // SRC_STORAGE_VOLUME_IMAGE_UTILS_DECOMPRESSOR_H_
