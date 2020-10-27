// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_CHUNKED_COMPRESSION_CHUNKED_DECOMPRESSOR_H_
#define SRC_LIB_CHUNKED_COMPRESSION_CHUNKED_DECOMPRESSOR_H_

#include <memory>

#include <fbl/array.h>
#include <fbl/function.h>
#include <fbl/macros.h>

#include "chunked-archive.h"
#include "status.h"

namespace chunked_compression {

// ChunkedDecompressor allows chunked archives to be decompressed (either a frame at a time, or in
// full).
//
// Usage (error checks omitted):
//
//   // Load the header into memory.
//   const void* header = InputDataHeader();
//   size_t header_length = 8192; // Read-ahead size for the file header
//
//   size_t compressed_length = InputLength(); // Assume >8192
//
//   HeaderReader reader;
//   SeekTable table = reader.Parse(header, header_length, compressed_length);
//
//   ChunkedDecompressor decompressor;
//
//   size_t target_offset = TargetOffset();
//   unsigned table_index = table.EntryForDecompressedOffset(TargetOffset()).get();
//   const SeekTableEntry& entry = table.Entries()[table_index];
//   fbl::Array<uint8_t> output_buffer(new uint8_t[entry.decompressed_size],
//                                     entry.decompressed_size);
//
//   size_t input_chunk_size = entry.compressed_size;
//   const uint8_t *input_chunk = LoadCompressedData(entry.compressed_offset, input_chunk_size);
//   size_t bytes_written;
//   decompressor.DecompressFrame(table, table_index, input_chunk, input_chunk_size,
//                                output_buffer.get(), output_buffer.size(), &bytes_written);
class ChunkedDecompressor {
 public:
  ChunkedDecompressor();
  ~ChunkedDecompressor();
  ChunkedDecompressor(ChunkedDecompressor&& o) = default;
  ChunkedDecompressor& operator=(ChunkedDecompressor&& o) = default;
  DISALLOW_COPY_AND_ASSIGN_ALLOW_MOVE(ChunkedDecompressor);

  // Convenience method to do a one-shot decompression of |input|, returning an allocated buffer
  // containing the decompressed bytes.
  static Status DecompressBytes(const void* input, size_t len, fbl::Array<uint8_t>* output,
                                size_t* bytes_written_out);

  // Returns the minimum size that a buffer must be to hold the result of decompressing the archive
  // described by |table|.
  static size_t ComputeOutputSize(const SeekTable& table) { return table.DecompressedSize(); }

  // Reads the decompressed archive described by |table| from |input|, and writes the decompressed
  // data to |output|.
  //
  // |input| should include the full archive contents, including the table itself. The table is
  // not validated (having already been validated during construction of |table|).
  // |output_len| must be at least |ComputeOutputSize(table)| bytes long.
  //
  // Returns the number of decompressed bytes written in |bytes_written_out|.
  Status Decompress(const SeekTable& table, const void* input, size_t len, void* output,
                    size_t output_len, size_t* bytes_written_out);

  // |input_frame| should start at the frame's first byte, and |input_frame_len| must be big enough
  // to span the entire frame.
  // |output| starts at the first byte to write the result, and |output_len| must be the resulting
  // decompressed size.
  //
  // Returns the number of decompressed bytes written in |bytes_written_out|.
  Status DecompressFrame(const void* input_frame, size_t input_frame_len, void* output,
                         size_t output_len, size_t* bytes_written_out);

  // Reads the |table_index|'th frame of the decompressed archive described by |table| from
  // |input_frame|, and writes the decompressed frame to |output|.
  //
  // |input_frame| should start at the frame's first byte, and |input_frame_len| must be big enough
  // to span the entire frame.
  // |output_len| must be at least as big as |table.Entries()[table_index].decompressed_size|.
  //
  // Returns the number of decompressed bytes written in |bytes_written_out|.
  Status DecompressFrame(const SeekTable& table, unsigned table_index, const void* input_frame,
                         size_t input_frame_len, void* output, size_t output_len,
                         size_t* bytes_written_out);

 private:
  struct DecompressionContext;
  std::unique_ptr<DecompressionContext> context_;
};

}  // namespace chunked_compression

#endif  // SRC_LIB_CHUNKED_COMPRESSION_CHUNKED_DECOMPRESSOR_H_
