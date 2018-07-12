// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_MEDIA_MEDIA_PLAYER_DECODE_SOFTWARE_DECODER_H_
#define GARNET_BIN_MEDIA_MEDIA_PLAYER_DECODE_SOFTWARE_DECODER_H_

#include <mutex>

#include <lib/async-loop/cpp/loop.h>
#include <lib/async/cpp/task.h>
#include <lib/async/default.h>
#include <lib/async/dispatcher.h>

#include "garnet/bin/media/media_player/decode/decoder.h"
#include "garnet/bin/media/media_player/metrics/value_tracker.h"
#include "lib/fxl/synchronization/thread_annotations.h"

namespace media_player {

// Abstract base class for software decoders.
//
// This base class implements a simple model for packet transformation on a
// worker thread. Most member variables are accessed on the graph's main thread
// exclusively. The worker and main thread communicate via posted tasks.
//
// The worker's sole responsibility is to process a single input packet when
// |HandleInputPacketOnWorker| is called. The worker posts a call to
// |HandleOutputPacket| to the main thread for each output packet it generates.
// When the input packet is completely processed, the worker posts a call to
// |WorkerDoneWithInputPacket| to the main thread. Any number of calls to
// |HandleOutputPacket| may result from a single |HandleInputPacketOnWorker|
// call.
//
// The main thread logic, under normal operation, maintains an input packet in
// |input_packet_| so it's ready for decoding. When an output packet is
// requested, the main thread logic posts a call to |HandleInputPacketOnWorker|
// passing the input packet. A request for a new input packet is issued at that
// time.
//
// Downstream nodes (the renderer, probably) is responsible for requesting
// packets early enough to make sure it doesn't starve.
//
// The main thread logic uses packets delivered via |HandleOutputPacket| to
// satisfy the output packet request. Any additional output packets produced
// are queued by the stage. If |WorkerDoneWithInputPacket| is called without
// any output packet being delivered, the cycle begins again.
//
// Exceptions to this behavior are many:
// 1) Initially and after a flush, |input_packet_| is not proactively filled.
//    The initial request for an output packet causes the main thread logic to
//    request an input packet. |PutInputPacket| is called when it arrives.
// 2) A request for an output packet may arrive when |input_packet_| is empty.
//    In this case, the main thread logic yields until an input packet, arrives
//    via |PutInputPacket|.
// 3) Input packets from upstream and output packets from the worker thread are
//    discarded when the node is flushing.
// 4) The main thread logic desists from requesting input packets after an
//    end-of-stream input packet arrives (until the input is flushed). When an
//    end-of-stream input packet is processed by the worker, it must produce
//    an end-of-stream output packet immediately before posting
//    |WorkerDoneWithInputPacket|.
//
class SoftwareDecoder : public Decoder {
 public:
  SoftwareDecoder();

  ~SoftwareDecoder() override;

  // AsyncNode implementation.
  void Dump(std::ostream& os) const override;

  void GetConfiguration(size_t* input_count, size_t* output_count) override;

  void FlushInput(bool hold_frame, size_t input_index,
                  fit::closure callback) override;

  void FlushOutput(size_t output_index, fit::closure callback) override;

  std::shared_ptr<PayloadAllocator> allocator_for_input(
      size_t input_index) override;

  void PutInputPacket(PacketPtr packet, size_t input_index) override;

  bool can_accept_allocator_for_output(size_t output_index) const override;

  void SetAllocatorForOutput(std::shared_ptr<PayloadAllocator> allocator,
                             size_t output_index) override;

  void RequestOutputPacket() override;

 protected:
  void PostTaskToMainThread(fit::closure task) const {
    async::PostTask(main_thread_dispatcher_, std::move(task));
  }

  void PostTaskToWorkerThread(fit::closure task) const {
    async::PostTask(worker_loop_.dispatcher(), std::move(task));
  }

  bool is_main_thread() const {
    return async_get_default_dispatcher() == main_thread_dispatcher_;
  }

  bool is_worker_thread() const {
    return async_get_default_dispatcher() == worker_loop_.dispatcher();
  }

  const std::shared_ptr<PayloadAllocator>& allocator() const {
    return allocator_;
  }

  // Notifies that a flush has occurred on the worker thread.
  virtual void Flush() {}

  // Processes a packet on the worker thread. Returns true to indicate we're
  // done processing the input packet. Returns false to indicate the input
  // packet should be processed again. |new_input| indicates whether the input
  // packet is new (true) or is being processed again (false). An output
  // packet may or may not be generated for any given invocation of this
  // method. |*output| is always set by this method, possibly to null.
  //
  // This method must always 'progress' decoding in one way or another. That is,
  // either the result must be true or an output packet must be generated or
  // both.
  virtual bool TransformPacket(const PacketPtr& input, bool new_input,
                               PacketPtr* output) = 0;

 private:
  // |OutputState| indicates where we are with respect to satisifying a request
  // for an output packet:
  //     |kIdle|: no output packet has been requested, and we're not currently
  //         processing an input packet on the worker
  //     |kWaitingForInput|: an output packet has been requested, and we're
  //         waiting for an input packet to arrive before we can proceed
  //     |kWaitingForWorker|: we're waiting for the worker to produce the output
  //         packet
  //     |kWorkerNotDone|: the worker has satisfied the request, but is still
  //         processing the input packet and may produce more output packets
  // Note that |OutputState| is not intended to reflect the prefetch of an input
  // packet.
  enum class OutputState {
    kIdle,
    kWaitingForInput,
    kWaitingForWorker,
    kWorkerNotDone
  };

  // Processes a single input packet on the worker thread.
  void HandleInputPacketOnWorker(PacketPtr packet);

  // Delivers an output packet from |HandleInputPacketOnWorker| to the main
  // thread.
  void HandleOutputPacket(PacketPtr packet);

  // Indicates that the worker thread is done with an input packet submitted
  // via |HandleInputPacketOnWorker|. Called on the main thread.
  void WorkerDoneWithInputPacket();

  async_dispatcher_t* main_thread_dispatcher_;
  async::Loop worker_loop_;

  // These fields are accessed on the main thread only.
  OutputState output_state_;
  bool flushing_ = true;
  bool end_of_input_stream_ = false;
  bool end_of_output_stream_ = false;
  // When we're not flushed and the input stream hasn't ended, we endeavor to
  // keep a packet in |input_packet_| waiting to be decoded. That is, if
  // |flushing_| and |end_of_input_stream_| are false and |input_packet_| is
  // null, we can be sure we've requested an input packet from upstream.
  PacketPtr input_packet_;
  fit::closure flush_callback_;

  // |allocator_| is initialized on the main thread during prepare and isn't
  // changed until unprepare. The worker thread uses it to allocate payload
  // memory.
  std::shared_ptr<PayloadAllocator> allocator_;

  // |decode_duration_| is updated on the worker thread and read on the main
  // thread when |Dump| is called.
  mutable std::mutex decode_duration_mutex_;
  ValueTracker<int64_t> decode_duration_ FXL_GUARDED_BY(decode_duration_mutex_);
};

}  // namespace media_player

#endif  // GARNET_BIN_MEDIA_MEDIA_PLAYER_DECODE_SOFTWARE_DECODER_H_
