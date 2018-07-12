// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_EXAMPLES_MEDIA_USE_AAC_DECODER_CODEC_CLIENT_H_
#define GARNET_EXAMPLES_MEDIA_USE_AAC_DECODER_CODEC_CLIENT_H_

#include "codec_buffer.h"
#include "codec_output.h"

#include <fuchsia/mediacodec/cpp/fidl.h>

#include <lib/async-loop/cpp/loop.h>
#include <lib/fidl/cpp/binding.h>

#include <list>

// This class is just _a_ codec client, and should be read as an example only,
// and probably not a fully complete example either.  This class is just here
// to organize the code involved in seting up a Codec with input buffers and
// packets, feeding it input data in a single Stream, setting up the output
// buffers and packets, and ensuring that all input data is processed into
// output.
//
// A Codec client that wants to seek a logical stream or re-use a Codec to
// decode another logical stream would likely want to make more use of the
// stream_lifetime_ordinal to feed input data and to accept output data (vs.
// this example which has only one Stream lifetime which isn't visible outside
// this class. Re-using a Codec instance for a new stream is encouraged,
// especially when the output format isn't likely to change from stream to
// stream, which avoids re-configuring output buffers across the stream switch.
//
// The use of particular threads of execution to call this class is intended to
// clarify reasonable ordering and concurrency of messages sent and processed on
// the Codec interface.  There is no requirement that a Codec client use
// dedicated threads to achieve a permitted and useful ordering. The client does
// of course need to stay within the sequencing rules of the interface.
class CodecClient {
 public:
  // loop - The loop that all the FIDL work will run on.  We configure this
  // explicitly instead of using the default loop per thread mechanism, because
  // we want to be very sure that we'll be posting to the correct loop to send
  // messages using that loop's single thread, as ProxyController doesn't have
  // a lock_ in it.
  CodecClient(async::Loop* loop);
  ~CodecClient();

  // Separate from Start() because we don't wan this class to handle the Codec
  // creation, so the caller needs a server endpoint to send off to a Codec
  // server (via the CodecFactory).
  fidl::InterfaceRequest<fuchsia::mediacodec::Codec> GetTheRequestOnce();

  // Get the Codec into a state where it's ready to process input data.
  void Start();

  // On this thread, wait for an available input packet_index, and when one is
  // available, create a new CodecPacket object to represent that packet_index
  // and return that.  The packet_index will be filled out, but not the rest of
  // the packet.  It's up to the caller to set stream_lifetime_ordinal and other
  // fields.
  //
  // Since in this example we're using a buffer per packet, waiting for a free
  // packet is also waiting for a free buffer, with the same index as
  // packet_index.
  //
  // To return eventually, this call relies on output being accepted on an
  // ongoing basis from the Codec using some other thread(s), processed, and
  // those output packets freed back to the codec.
  std::unique_ptr<fuchsia::mediacodec::CodecPacket>
  BlockingGetFreeInputPacket();

  const CodecBuffer& GetInputBufferByIndex(uint32_t packet_index);
  const CodecBuffer& GetOutputBufferByIndex(uint32_t packet_index);

  // Queue an input packet to the codec.
  void QueueInputPacket(
      std::unique_ptr<fuchsia::mediacodec::CodecPacket> packet);

  void QueueInputEndOfStream(uint64_t stream_lifetime_ordinal);

  // Use the current thread to do what is necessary to get an ouput packet.
  // Near the start, this will include configuring output buffers once.  In
  // steady state this thread will just wait for an output packet to show up or
  // the stream to be done.  If an end_of_stream packet shows up, this method
  // will return that packet.
  //
  // The returned CodecPacket itself will remain valid and readable as long as
  // the caller keeps it around.  However, if the caller calls
  // BlockingGetEmittedOutput() again, the entire set of output buffers
  // can get deallocated and replacement buffers allocated, rendering the
  // meaning of the returned CodecPacket only usable until
  // BlockingGetEmittedOutput() is called again.  This means the calling
  // code needs to go ahead and do whatever it wants to do with the output
  // data in the corresponding output buffer before calling this method again.
  //
  // A real client can delay output buffer re-configuration until previous
  // output data has been fully processed, or can ensure that old output buffers
  // remain live until the old output data is done with them (configuring new
  // output buffers doesn't inherently delete the old ones, but having both
  // around at once does use more resources concurrently).
  std::unique_ptr<CodecOutput> BlockingGetEmittedOutput();

  // Recycle an output packet for re-use.
  void RecycleOutputPacket(fuchsia::mediacodec::CodecPacketHeader free_packet);

  void Stop();

  // On this thread, while the codec is being fed input data on
 private:
  friend class CodecStream;

  void CallSyncAndWaitForResponse();

  //
  // Events:
  //

  void OnStreamFailed(uint64_t stream_lifetime_ordinal);

  void OnInputConstraints(
      fuchsia::mediacodec::CodecBufferConstraints input_constraints);
  void OnFreeInputPacket(
      fuchsia::mediacodec::CodecPacketHeader free_input_packet);

  // This example ignores any buffer constraints with
  // buffer_constraints_action_required false.
  //
  // As with any proper Codec client we must tolerate this event getting sent by
  // the server more times than would be necessary if it were only for the
  // client's benefit.  The server is allowed to force an output buffer
  // re-configuration just because it wants one.  This rule simplifies some
  // codec server implementations substantially and allows increased coverage of
  // format change handling in clients, at least in the sense of ever seeing
  // more than one of this message per Codec instance (though not quite to the
  // degree needed to fully cover client handling of true mid-stream format
  // changes).
  void OnOutputConfig(fuchsia::mediacodec::CodecOutputConfig output_config);

  // Every output packet is stream-specific with stream_lifetime_ordinal set.
  void OnOutputPacket(fuchsia::mediacodec::CodecPacket output_packet,
                      bool error_detected_before, bool error_detected_during);

  void OnOutputEndOfStream(uint64_t stream_lifetime_ordinal,
                           bool error_detected_before);
  std::mutex lock_;
  async::Loop* loop_ = nullptr;  // must override
  async_dispatcher_t* dispatcher_ = nullptr;     // must override
  fuchsia::mediacodec::CodecPtr codec_;
  // This only temporarily holds the Codec request that was created during the
  // constructor.  If the caller asks for this more than once, the subsequent
  // requests give back a !is_valid() request.
  fidl::InterfaceRequest<fuchsia::mediacodec::Codec> temp_codec_request_;

  // We're use unique_ptr<> here only for it's optional-ness.
  std::unique_ptr<fuchsia::mediacodec::CodecBufferConstraints>
      input_constraints_;
  std::condition_variable input_constraints_exist_condition_;

  // In this example, we use buffer-per-packet mode, but for input buffers it
  // is allowed to share parts of a single buffer across all input packets.
  // This example doesn't yet demonstrate that mode however.
  //
  // TODO(dustingreen): There's not presently any input mode that allows any
  // packet to refer to any of a set of multiple buffers, but if we think that
  // would be of any real use, we could add it.  Either add that mode to Codec
  // interface and down, or remove this comment.
  //
  // The index into the vector is the saem as packet_id, since we're running in
  // buffer-per-packet mode.
  std::vector<std::unique_ptr<CodecBuffer>> all_input_buffers_;
  // We don't even create the output buffers until after the output format is
  // known, which can requier some input data first.
  std::vector<std::unique_ptr<CodecBuffer>> all_output_buffers_;

  // In contrast to buffers, packets don't really exist in full continuously.
  // The set of packet_index values is a thing, but each packet_index re-use is
  // really best thought of as a new fuchsia::mediacodec::CodecPacket lifetime,
  // so that's how we represent the packets in this example - as created and
  // owned by their input and output arcs, not kept allocated continuously by
  // CodecClient.

  // This vector is used to track which input packet_id(s) are free.  A free
  // packet_id means the buffer at all_input_buffers_[packet_id] is free.  We
  // push to the end and pop from the end since that's what vector<> is good at.
  std::vector<uint32_t> input_free_list_;
  std::condition_variable input_free_list_not_empty_;

  // In this example, we do verify that the server is being sane with respect
  // to free/busy status of packets.  In general a client shouldn't let a
  // badly-behaved server cause the client to crash.
  //
  // true - free
  // false - not free (from when we queue a lambda that'll end up sending the
  //   packet to the codec, to when we receive the message from the codec saying
  //   the packet is free again)
  std::vector<bool> input_free_bits_;

  // Which output packets are free from the client point of view.  If the server
  // tries to emit the same packet more than once concurrently, these bits are
  // how we notice.
  std::vector<bool> output_free_bits_;

  // In contrast to free input packets, we care about the content of emitted
  // output packets and their order.  In addition, OnOutputConfig() is ordered
  // with respect to output packets, so we just queue those along with the
  // output packets to avoid any ambiguity.
  //
  // A client that is immediately processing every output packet and just tracks
  // the most recent output config would work as long as it always associates
  // an output packet with the closest prior CodecOutputConfig.
  std::list<std::unique_ptr<CodecOutput>> emitted_output_;

  // For input, in this example we just know what the input format details are
  // and we send those to CodecFactory as part of CreateAudioDecoder_Params,
  // so we don't really need them as a member variable.

  // For output, we have CodecOutputConfig here as a shared_ptr<> so we can
  // explicitly associate each output packet with the config that applies to the
  // output packet.
  //
  // Note that stream_lifetime_ordinal is nearly entirely orthogonal from which
  // config applies.  The only interaction is that sometimes a new stream will
  // happen to have a different format so will cause format_details to update.
  std::shared_ptr<const fuchsia::mediacodec::CodecOutputConfig>
      last_output_config_;
  std::shared_ptr<const fuchsia::mediacodec::CodecOutputConfig>
      last_required_output_config_;
  // Becomes true when we get a new last_output_config_ with action required,
  // and becomes false just before taking the needed action based on
  // last_output_config_.
  bool output_config_action_pending_ = false;
  // Only odd values are allowed for buffer_lifetime_ordinal.
  uint64_t next_output_buffer_lifetime_ordinal_ = 1;

  // Invariant:
  // output_pending_ == (!emitted_output_.empty() ||
  // output_config_action_pending_)
  bool ComputeOutputPendingLocked() {
    return !emitted_output_.empty() || output_config_action_pending_;
  }
  bool output_pending_;
  std::condition_variable output_pending_condition_;
};

#endif  // GARNET_EXAMPLES_MEDIA_USE_AAC_DECODER_CODEC_CLIENT_H_
