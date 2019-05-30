// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_MEDIA_CODECS_OUTPUT_SINK_H_
#define GARNET_BIN_MEDIA_CODECS_OUTPUT_SINK_H_

#include <lib/fit/defer.h>
#include <lib/fit/function.h>
#include <lib/media/codec_impl/codec_buffer.h>
#include <lib/media/codec_impl/codec_packet.h>
#include <threads.h>

#include "mpsc_queue.h"

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
  enum SendStatus {
    kSent = 0,
    kError = 1,
  };

  using Sender = fit::function<SendStatus(CodecPacket* output_packet)>;

  // Output blocks are slices of the underlying packet and buffer.
  //
  // Output blocks will not overlap with one another, and are vended in order.
  struct OutputBlock {
    uint8_t* data;
    size_t len;
  };

  enum Status {
    kOk = 0,
    kUserTerminatedWait = 1,
    kBuffersTooSmall = 2,
    kSendError = 3,
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

  // Returns a pointer to the next block in the output stream that is
  // `write_size` in length. The output block is only valid if status is `kOk`.
  //
  // OutputBlocks are valid until the next call to NextOutputBlock.
  //
  // The containing packet will be sent when flushed or when it has no room for
  // the next write.
  //
  // When there are not enough output packets or output buffers to satisfy a
  // request, this call will block until the needed resources are added or a
  // call to `StopAllWaits()` terminates the wait.
  //
  // This should only be called on the writer thread.
  std::pair<OutputBlock, Status> NextOutputBlock(
      size_t write_size, std::optional<uint64_t> timestamp);

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

#endif  // GARNET_BIN_MEDIA_CODECS_OUTPUT_SINK_H_
