// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_CHUNKED_COMPRESSION_STREAMING_CHUNKED_COMPRESSOR_H_
#define SRC_LIB_CHUNKED_COMPRESSION_STREAMING_CHUNKED_COMPRESSOR_H_

#include <memory>
#include <optional>

#include <fbl/array.h>
#include <fbl/function.h>
#include <fbl/macros.h>

#include "chunked-archive.h"
#include "compression-params.h"
#include "status.h"

namespace chunked_compression {

// StreamingChunkedCompressor creates compressed archives by reading a stream of data which has
// a known size ahead of time.
//
// Usage (error checks omitted):
//
//   size_t input_data_sz = InputDataSize();
//
//   StreamingChunkedCompressor compressor;
//   size_t output_limit = compressor.ComputeOutputSizeLimit(input_data_sz);
//
//   fbl::Array<uint8_t> output_buffer(new uint8_t[output_limit], output_limit);
//   compressor.Init(input_data_sz, output_buffer.get(), output_buffer.size());
//
//   uint8_t input_buffer[ReadBufferSize()];
//   size_t bytes_in_buffer;
//   while ((bytes_in_buffer = ReadInput(input_buffer, sizeof(input_buffer)))) {
//     compressor.Update(input_buffer, bytes_in_buffer);
//   }
//
//   size_t compressed_size;
//   compressor.Final(&compressed_size);
class StreamingChunkedCompressor {
 public:
  StreamingChunkedCompressor();
  explicit StreamingChunkedCompressor(CompressionParams params);
  ~StreamingChunkedCompressor();
  StreamingChunkedCompressor(StreamingChunkedCompressor&& o);
  StreamingChunkedCompressor& operator=(StreamingChunkedCompressor&& o);
  DISALLOW_COPY_AND_ASSIGN_ALLOW_MOVE(StreamingChunkedCompressor);

  // Returns the minimum size that a buffer must be to hold the result of compressing |len| bytes.
  size_t ComputeOutputSizeLimit(size_t len) { return params_.ComputeOutputSizeLimit(len); }

  // Initializes the compressor to prepare to recieve |stream_len| bytes of input data.
  //
  // The compressed data will be written to |output|. |output_len| must be at least
  // |ComputeOutputSizeLimit(stream_len)| bytes.
  //
  // If |Init| is invoked while compression is ongoing, the context of the previous compression is
  // reset and the previous output buffer is left in an undefined state.
  Status Init(size_t stream_len, void* output, size_t output_len);

  // Processes exactly |input_len| bytes of input data, read from |input|.
  //
  // If |input_len| bytes would take the streaming compressor past the end of the expected data
  // length (i.e. the |stream_len| parameter to the previous call to |Init|), then an error is
  // returned.
  Status Update(const void* input, size_t input_len);

  // Finalizes the compressed archive, returning its size in |compressed_size_out|.
  //
  // |Final| must be called before the compressed archive is usable, and |Final| must only
  // be called after the entire input has been processed.
  //
  // The compressor is reusable after |Final| is called by invoking |Init| again.
  Status Final(size_t* compressed_size_out);

  // Registers |callback| to be invoked after each frame is complete.
  using ProgressFn =
      fbl::Function<void(size_t bytes_read, size_t bytes_total, size_t bytes_written)>;
  void SetProgressCallback(ProgressFn callback) { progress_callback_ = std::move(callback); }

 private:
  // Must be called before each new frame is written to, and can only be called when |input_offset_|
  // falls on a frame boundary.
  Status StartFrame();
  // Must be called after each frame is completed.
  Status EndFrame(size_t uncompressed_frame_start, size_t uncompressed_frame_len);
  // Appends |len| bytes to the current frame. |len| must be less than the expected size of the
  // frame.
  // Calls EndFrame if the frame was completed by this data, and then calls StartFrame if there is
  // still more data expected in the input stream.
  Status AppendToFrame(const void* data, size_t len);

  void MoveFrom(StreamingChunkedCompressor&& o);

  uint8_t* compressed_output_ = nullptr;
  size_t compressed_output_len_;
  size_t compressed_output_offset_;

  size_t input_len_;
  size_t input_offset_;

  HeaderWriter header_writer_;

  std::optional<ProgressFn> progress_callback_;

  CompressionParams params_;

  struct CompressionContext;
  std::unique_ptr<CompressionContext> context_;
};

}  // namespace chunked_compression

#endif  // SRC_LIB_CHUNKED_COMPRESSION_STREAMING_CHUNKED_COMPRESSOR_H_
