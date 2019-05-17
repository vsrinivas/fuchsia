// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_MEDIA_CODECS_CHUNK_INPUT_STREAM_H_
#define GARNET_BIN_MEDIA_CODECS_CHUNK_INPUT_STREAM_H_

#include <lib/fit/defer.h>
#include <lib/media/codec_impl/codec_buffer.h>
#include <lib/media/codec_impl/codec_packet.h>

#include <optional>
#include <vector>

#include "timestamp_extrapolator.h"

// A chunk iterator for a stream of input packets. Provides fixed size input
// blocks from the stream of input packets, buffering the end of input packets
// that don't align with the block size until another packet arrives to complete
// the block.
//
// `ChunkInputStream` will extrapolate timestamps with the provided extrapolator
// if the input packet's timestamp does not align with the block size. See
// `TimestampExtrapolator` for extrapolation semantics.
class ChunkInputStream {
 public:
  struct InputBlock {
    // Pointer to data which is set iff `len > 0`.
    const uint8_t* data = nullptr;
    const size_t len = 0;
    const size_t non_padding_len = 0;
    // Set on the last invocation of the input block processor for the input
    // stream.
    const bool is_end_of_stream = false;
    const std::optional<uint64_t> timestamp_ish;
  };

  enum Status {
    kOk = 0,
    kUserTerminated = 1,
    kExtrapolationFailedWithoutTimebase = 2,
  };

  enum ControlFlow {
    kContinue = 0,
    kTerminate = 1,
  };

  using InputBlockProcessor =
      fit::function<ControlFlow(InputBlock input_block)>;

  ChunkInputStream(size_t chunk_size,
                   TimestampExtrapolator&& timestamp_extrapolator,
                   InputBlockProcessor&& input_block_processor);

  // Adds a new input packet to the input stream and executes
  // `input_block_processor` for all the newly available input blocks (which may
  // be none).
  //
  // Pointers in an input block should not be stored. They are valid only for
  // their lifetime as an argument to the `input_block_processor`.
  //
  // If `input_block_processor` returns `kTerminate`, iteration over input
  // blocks will stop. After this early termination, all calls to this instance
  // other than ~ChunkInputStream will fail with an `ASSERT` in debug builds.
  Status ProcessInputPacket(const CodecPacket* input_packet);

  // If there are any buffered input bytes, `Flush` will pad the input block
  // with 0s to complete it and yield an input block with the content.
  //
  // The flushed input block, which may or may not have data, will be sent to
  // `flush_block_processor` if provided or `input_block_processor` otherwise.
  // The flushed block will have `is_end_of_stream` set to `true`.
  Status Flush();

 private:
  struct ScratchBlock {
    std::vector<uint8_t> data;
    size_t len = 0;
    bool full() const { return len == data.size(); }
    bool empty() const { return len == 0; }
    uint8_t* empty_start() {
      ZX_DEBUG_ASSERT(empty_bytes_left() > 0);
      return data.data() + len;
    }
    size_t empty_bytes_left() const {
      ZX_DEBUG_ASSERT(len <= data.size());
      return data.size() - len;
    }
  };

  struct InputPacket {
    const CodecPacket* packet = nullptr;
    size_t offset = 0;
    const uint8_t* data_at_offset() const {
      ZX_DEBUG_ASSERT(bytes_unread() > 0);
      return packet->buffer()->buffer_base() + packet->start_offset() + offset;
    }
    size_t bytes_unread() const {
      ZX_DEBUG_ASSERT(packet);
      ZX_DEBUG_ASSERT(offset <= packet->valid_length_bytes());
      return packet->valid_length_bytes() - offset;
    }
  };

  // Appends bytes from the input packet to the scratch block until the block
  // runs out of space or the packet runs out of bytes.
  void AppendToScratchBlock(InputPacket* input_packet);

  // Emits a block to the user's `InputBlockProcessor`.
  Status EmitBlock(const uint8_t* data, const size_t non_padding_len,
                   const bool is_end_of_stream = false);

  // Ensures we have a timestamp in `next_output_timestamp_` for the next
  // emitted block.
  Status EnsureTimestamp();

  // Returns total number of bytes seen, which may be more than `stream_index_`,
  // because we might have some bytes in the scratch block.
  size_t BytesSeen() const;

  const size_t chunk_size_ = 0;
  TimestampExtrapolator timestamp_extrapolator_;
  const InputBlockProcessor input_block_processor_ = nullptr;

  // The next output timestamp we will attach when emitting a block.
  std::optional<uint64_t> next_output_timestamp_;

  // Index in the input stream we've emitted so far.
  size_t stream_index_ = 0;
  // Temporary space to hold input bytes unaligned with the chunk_size_ until
  // we get more input bytes to complete the chunk, or flush.
  ScratchBlock scratch_block_;

  // Whether the user early-terminated the stream when processing an input
  // block.
  bool early_terminated_ = false;
};

#endif  // GARNET_BIN_MEDIA_CODECS_CHUNK_INPUT_STREAM_H_
