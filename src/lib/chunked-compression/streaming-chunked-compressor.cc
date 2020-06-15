// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/syslog/cpp/macros.h>
#include <zircon/assert.h>

#include <algorithm>

#include <fbl/algorithm.h>
#include <src/lib/chunked-compression/chunked-archive.h>
#include <src/lib/chunked-compression/chunked-compressor.h>
#include <src/lib/chunked-compression/status.h>
#include <src/lib/chunked-compression/streaming-chunked-compressor.h>
#include <zstd/zstd.h>

namespace chunked_compression {

struct StreamingChunkedCompressor::CompressionContext {
  CompressionContext() = default;
  explicit CompressionContext(ZSTD_CCtx* ctx) : inner_(ctx) {}
  ~CompressionContext() { ZSTD_freeCCtx(inner_); }

  size_t current_output_frame_start_ = 0ul;
  size_t current_output_frame_relative_pos_ = 0ul;

  ZSTD_CCtx* inner_;
};

StreamingChunkedCompressor::StreamingChunkedCompressor()
    : StreamingChunkedCompressor(CompressionParams{}) {}

StreamingChunkedCompressor::StreamingChunkedCompressor(CompressionParams params)
    : params_(params), context_(std::make_unique<CompressionContext>(ZSTD_createCCtx())) {
  // TODO: create factory method, return status instead of asserting.
  ZX_ASSERT(params.IsValid());
}

StreamingChunkedCompressor::~StreamingChunkedCompressor() {}

StreamingChunkedCompressor::StreamingChunkedCompressor(StreamingChunkedCompressor&& o) {
  MoveFrom(std::move(o));
}

StreamingChunkedCompressor& StreamingChunkedCompressor::operator=(StreamingChunkedCompressor&& o) {
  MoveFrom(std::move(o));
  return *this;
}

void StreamingChunkedCompressor::MoveFrom(StreamingChunkedCompressor&& o) {
  compressed_output_ = o.compressed_output_;
  o.compressed_output_ = nullptr;

  compressed_output_len_ = o.compressed_output_len_;
  compressed_output_offset_ = o.compressed_output_offset_;
  o.compressed_output_offset_ = 0ul;

  input_len_ = o.input_len_;
  input_offset_ = o.input_offset_;
  o.input_offset_ = 0ul;

  header_writer_ = std::move(o.header_writer_);

  progress_callback_ = std::move(o.progress_callback_);

  params_ = o.params_;

  context_ = std::move(o.context_);
}

Status StreamingChunkedCompressor::Init(size_t stream_len, void* output, size_t output_len) {
  size_t num_frames = HeaderWriter::NumFramesForDataSize(stream_len, params_.chunk_size);
  size_t metadata_size = HeaderWriter::MetadataSizeForNumFrames(num_frames);
  if (metadata_size > output_len) {
    return kStatusErrBufferTooSmall;
  }

  size_t r = ZSTD_initCStream(context_->inner_, params_.compression_level);
  if (ZSTD_isError(r)) {
    FX_LOGS(ERROR) << "Failed to init stream";
    return kStatusErrInternal;
  }
  if (params_.frame_checksum) {
    r = ZSTD_CCtx_setParameter(context_->inner_, ZSTD_c_checksumFlag, 1);
    if (ZSTD_isError(r)) {
      FX_LOGS(ERROR) << "Failed to init stream";
      return kStatusErrInternal;
    }
  }

  compressed_output_ = static_cast<uint8_t*>(output);
  compressed_output_len_ = output_len;
  compressed_output_offset_ = metadata_size;

  input_len_ = stream_len;
  input_offset_ = 0ul;

  Status status = StartFrame();
  if (status != kStatusOk) {
    compressed_output_ = nullptr;
    return status;
  }

  status = HeaderWriter::Create(output, metadata_size, num_frames, &header_writer_);
  if (status != kStatusOk) {
    compressed_output_ = nullptr;
    return status;
  }

  return kStatusOk;
}

Status StreamingChunkedCompressor::Update(const void* input, size_t len) {
  if (compressed_output_ == nullptr) {
    return kStatusErrBadState;
  } else if (len > input_len_ - input_offset_) {
    // |len| takes us past the expected end of the input stream
    return kStatusErrInvalidArgs;
  }

  size_t consumed = 0;
  // Consume input up to one input frame at a time.
  while (consumed < len) {
    const size_t bytes_left = len - consumed;

    const size_t current_frame_start = fbl::round_down(input_offset_, params_.chunk_size);
    const size_t current_frame_end = std::min(current_frame_start + params_.chunk_size, input_len_);
    const size_t bytes_left_in_current_frame = current_frame_end - input_offset_;

    const size_t bytes_to_consume = std::min(bytes_left, bytes_left_in_current_frame);

    Status status = AppendToFrame(static_cast<const uint8_t*>(input) + consumed, bytes_to_consume);
    if (status != kStatusOk) {
      return status;
    }
    consumed += bytes_to_consume;
  }
  return kStatusOk;
}

Status StreamingChunkedCompressor::Final(size_t* compressed_size_out) {
  if (compressed_output_ == nullptr) {
    return kStatusErrBadState;
  }

  if (input_offset_ < input_len_) {
    // Final() was called before the entire input was processed.
    return kStatusErrBadState;
  }
  // There should not be any pending output frames.
  ZX_DEBUG_ASSERT(context_->current_output_frame_relative_pos_ == 0ul);

  Status status = header_writer_.Finalize();
  if (status == kStatusOk) {
    *compressed_size_out = compressed_output_offset_;
  }
  return status;
}

Status StreamingChunkedCompressor::StartFrame() {
  ZX_DEBUG_ASSERT(context_->current_output_frame_relative_pos_ == 0ul);

  context_->current_output_frame_start_ = compressed_output_offset_;

  // Since we know the data size in advance we can optimize compression by hinting the size
  // to zstd. This will make the entire chunk be written as a single data frame
  size_t next_chunk_size = std::min(params_.chunk_size, input_len_ - input_offset_);
  size_t r = ZSTD_CCtx_reset(context_->inner_, ZSTD_reset_session_only);
  if (ZSTD_isError(r)) {
    return kStatusErrInternal;
  }
  r = ZSTD_CCtx_setPledgedSrcSize(context_->inner_, next_chunk_size);
  if (ZSTD_isError(r)) {
    return kStatusErrInternal;
  }

  return kStatusOk;
}

Status StreamingChunkedCompressor::EndFrame(size_t uncompressed_frame_start,
                                            size_t uncompressed_frame_len) {
  ZX_DEBUG_ASSERT(uncompressed_frame_start % params_.chunk_size == 0);

  SeekTableEntry entry;
  entry.decompressed_offset = uncompressed_frame_start;
  entry.decompressed_size = uncompressed_frame_len;
  entry.compressed_offset = context_->current_output_frame_start_;
  entry.compressed_size = compressed_output_offset_ - context_->current_output_frame_start_;
  Status status = header_writer_.AddEntry(entry);
  if (status != kStatusOk) {
    return status;
  }

  context_->current_output_frame_relative_pos_ = 0ul;

  if (progress_callback_) {
    (*progress_callback_)(input_offset_, input_len_, compressed_output_offset_);
  }

  return kStatusOk;
}

Status StreamingChunkedCompressor::AppendToFrame(const void* data, size_t len) {
  const size_t current_frame_start = fbl::round_down(input_offset_, params_.chunk_size);
  const size_t current_frame_end = std::min(current_frame_start + params_.chunk_size, input_len_);

  const size_t bytes_left_in_current_frame = current_frame_end - input_offset_;
  ZX_DEBUG_ASSERT(len <= bytes_left_in_current_frame);

  const bool will_finish_frame = bytes_left_in_current_frame == len;

  ZSTD_inBuffer in_buf;
  in_buf.src = data;
  in_buf.size = len;
  in_buf.pos = 0ul;

  // |out_buf| is set up to be relative to the current output frame we are processing.
  ZSTD_outBuffer out_buf;
  // dst is the start of the frame.
  out_buf.dst = static_cast<uint8_t*>(compressed_output_) + context_->current_output_frame_start_;
  // size is the total number of bytes left in the output buffer, from the start of the current
  // frame.
  out_buf.size = compressed_output_len_ - context_->current_output_frame_start_;
  // pos is the progress past the start of the frame so far.
  out_buf.pos = context_->current_output_frame_relative_pos_;

  ZX_DEBUG_ASSERT(context_->current_output_frame_start_ + out_buf.size <= compressed_output_len_);
  ZX_DEBUG_ASSERT(out_buf.pos <= out_buf.size);

  size_t r = ZSTD_compressStream(context_->inner_, &out_buf, &in_buf);
  if (ZSTD_isError(r)) {
    FX_LOGS(ERROR) << "ZSTD_compressStream failed: " << ZSTD_getErrorName(r);
    return kStatusErrInternal;
  } else if (in_buf.pos < in_buf.size) {
    FX_LOGS(ERROR) << "Partial read";
    return kStatusErrInternal;
  }

  if (will_finish_frame) {
    r = ZSTD_endStream(context_->inner_, &out_buf);
    if (ZSTD_isError(r)) {
      FX_LOGS(ERROR) << "ZSTD_endStream failed: " << ZSTD_getErrorName(r);
      return kStatusErrInternal;
    }
  }

  input_offset_ += len;
  compressed_output_offset_ += (out_buf.pos - context_->current_output_frame_relative_pos_);

  if (will_finish_frame) {
    // Case 1: The frame is finished. Write the seek table entry and advance to the next output
    // frame.
    Status status = EndFrame(current_frame_start, current_frame_end - current_frame_start);
    if (status != kStatusOk) {
      FX_LOGS(ERROR) << "Failed to finalize frame";
      return status;
    }

    if (input_offset_ < input_len_) {
      status = StartFrame();
      if (status != kStatusOk) {
        FX_LOGS(ERROR) << "Failed to start next frame";
      }
    }
  } else {
    // Case 2: The frame isn't complete yet. Mark our progress.
    context_->current_output_frame_relative_pos_ = out_buf.pos;
  }

  return kStatusOk;
}

}  // namespace chunked_compression
