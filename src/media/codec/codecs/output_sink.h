// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_CODEC_CODECS_OUTPUT_SINK_H_
#define SRC_MEDIA_CODEC_CODECS_OUTPUT_SINK_H_

#include <lib/fit/defer.h>
#include <lib/fit/function.h>
#include <lib/media/codec_impl/codec_buffer.h>
#include <lib/media/codec_impl/codec_packet.h>
#include <threads.h>

#include "src/media/lib/mpsc_queue/mpsc_queue.h"

// A sink for blocks of output data that manages output packets and output
// buffers.
//
// An example use case:
//
//   while (input < input_end) {
//     auto [output_block, status] = output_sink.NextOutputBlock(output_size);
//     if (status != OutputSink::kOk) {
//       // handle error
//     }
//     encoder.EncodeInto(&input, output_block.data);
//   }
//
// This class is expected to be used on two or more threads: a writer thread
// that calls `NextOutputBlock` and `Flush`, and then any other thread(s), which
// can also be the writer thread. See comments on each method for thread safety
// guidance.
class OutputSink {
 public:
  enum UserStatus {
    kSuccess = 0,
    kError = 1,
  };

  using Sender = fit::function<UserStatus(CodecPacket* output_packet)>;

  // Output blocks are slices of the underlying packet and buffer.
  //
  // Output blocks will not overlap with one another, and are vended in order.
  // `buffer` is a reference to the underlying codec buffer that `data` points into.
  struct OutputBlock {
    uint8_t* data;
    size_t len;
    const CodecBuffer* buffer;
  };

  enum Status {
    kOk = 0,
    kUserTerminatedWait = 1,
    kBuffersTooSmall = 2,
    kUserError = 3,
  };

  // Constructs a new output sink that will use `sender` to emit complete or
  // flushed output packets.
  OutputSink(Sender sender, thrd_t writer_thread);

  // Adds an output packet to vend output blocks with. Packets must be added
  // when they are new and when they are recycled.
  //
  // This call is allowed from any thread at any time.
  void AddOutputPacket(CodecPacket* output_packet);

  // Adds an output buffer to vend output blocks with. Buffers need only be
  // added once.
  //
  // This call is allowed from any thread at any time.
  void AddOutputBuffer(const CodecBuffer* output_buffer);

  // Runs the given function, passing in the next output block of at least
  // `write_size` bytes.
  //
  // The function should return the amount of bytes actually written to the
  // block.
  //
  // OutputBlocks are valid for their lifetime as an argument and should not be
  // stashed.
  //
  // The containing packet will be sent when flushed or when it has no room for
  // the next write.
  //
  // When there are not enough output packets or output buffers to satisfy a
  // request, this call will block until the needed resources are added or a
  // call to `StopAllWaits()` terminates the wait.
  //
  // This should only be called on the writer thread.
  Status NextOutputBlock(
      size_t write_size, std::optional<uint64_t> timestamp_ish,
      fit::function<std::pair<size_t, UserStatus>(OutputBlock)> output_block_writer);

  // Flushes the current output packet even if it isn't full.
  //
  // This should only be called on the writer thread.
  Status Flush();

  // Stops all blocking calls from waiting. They will return a
  // `kUserTerminatedWait` status. This class will continue to
  // return `kUserTerminatedWait` instead of blocking until
  // `Reset` is called.
  //
  // This call is allowed from any thread.
  void StopAllWaits();

  // Resets the stream, re-arming it for waits.
  //
  // If `keep_data` is true, the free buffers and packets will not be discarded.
  //
  // This call is allowed from any thread.
  void Reset(bool keep_data = false);

 private:
  bool CurrentPacketHasRoomFor(size_t write_size);

  Status SendCurrentPacket();

  Status SetNewPacketForWrite(size_t write_size);

  Sender sender_;
  const thrd_t writer_thread_;

  BlockingMpscQueue<CodecPacket*> free_output_packets_;
  BlockingMpscQueue<const CodecBuffer*> free_output_buffers_;

  CodecPacket* current_packet_ = nullptr;
};

#endif  // SRC_MEDIA_CODEC_CODECS_OUTPUT_SINK_H_
